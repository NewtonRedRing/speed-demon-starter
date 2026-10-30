#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct TransparentHash {
    using is_transparent = void;
    std::size_t operator()(std::string_view s) const noexcept {
        return std::hash<std::string_view>{}(s);
    }
};

struct TransparentEqual {
    using is_transparent = void;
    bool operator()(std::string_view a, std::string_view b) const noexcept {
        return a == b;
    }
};

struct Occurrence {
    std::uint32_t document;
    std::uint16_t position;
};

struct TermIndex {
    // Boolean-only terms never need positional postings.
    std::vector<Occurrence> occurrences;
    std::vector<std::uint32_t> documents;
    bool needs_positions = false;
};

struct Query {
    bool phrase = false;
    std::vector<std::uint32_t> terms;
    std::vector<char> operators;
};

using TermMap = std::unordered_map<std::string, std::uint32_t, TransparentHash, TransparentEqual>;

std::uint32_t get_term(std::string_view word, TermMap& ids, std::vector<TermIndex>& terms) {
    auto it = ids.find(word);
    if (it != ids.end()) return it->second;
    const auto id = static_cast<std::uint32_t>(terms.size());
    ids.emplace(std::string(word), id);
    terms.emplace_back();
    return id;
}

std::vector<std::string_view> split(std::string_view text) {
    std::vector<std::string_view> result;
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t end = text.find(' ', start);
        result.push_back(text.substr(start, end == std::string_view::npos ? text.size() - start : end - start));
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return result;
}

void add_document_token(std::string& token, std::uint32_t document, std::uint16_t& position,
                       const TermMap& ids, std::vector<TermIndex>& terms) {
    if (token.empty()) return;
    auto it = ids.find(token);
    if (it != ids.end()) {
        TermIndex& index = terms[it->second];
        // Build unique, sorted document IDs in the only scan of the corpus.
        if (index.documents.empty() || index.documents.back() != document)
            index.documents.push_back(document);
        if (index.needs_positions) index.occurrences.push_back({document, position});
    }
    ++position;
    token.clear();
}

void combine(const std::vector<std::uint32_t>& left, const std::vector<std::uint32_t>& right,
             char operation, std::vector<std::uint32_t>& output) {
    std::size_t i = 0, j = 0;
    output.clear();
    if (operation == 'O') output.reserve(left.size() + right.size());
    else output.reserve(std::min(left.size(), right.size()));

    // Adaptive exact intersection: when list lengths differ greatly, probe
    // the larger posting list instead of linearly scanning every element.
    // Correct for empty lists, duplicates-free ascending doc IDs, and all
    // possible operator orders (Boolean evaluation remains left-associative).
    if (operation == 'A') {
        const auto& small = left.size() < right.size() ? left : right;
        const auto& large = left.size() < right.size() ? right : left;
        if (large.size() > 16 * small.size()) {
            for (std::uint32_t doc : small) {
                if (std::binary_search(large.begin(), large.end(), doc))
                    output.push_back(doc);
            }
            return;
        }
    }
    while (i < left.size() && j < right.size()) {
        if (left[i] < right[j]) {
            if (operation != 'A') output.push_back(left[i]);
            ++i;
        } else if (right[j] < left[i]) {
            if (operation == 'O') output.push_back(right[j]);
            ++j;
        } else {
            if (operation != 'N') output.push_back(left[i]);
            ++i;
            ++j;
        }
    }
    if (operation == 'O') {
        output.insert(output.end(), left.begin() + i, left.end());
        output.insert(output.end(), right.begin() + j, right.end());
    } else if (operation == 'N') {
        output.insert(output.end(), left.begin() + i, left.end());
    }
}

bool has_occurrence(const TermIndex& term, std::uint32_t document, std::uint16_t position) {
    const Occurrence key{document, position};
    auto it = std::lower_bound(term.occurrences.begin(), term.occurrences.end(), key,
        [](const Occurrence& a, const Occurrence& b) {
            return a.document < b.document ||
                   (a.document == b.document && a.position < b.position);
        });
    return it != term.occurrences.end() && it->document == document && it->position == position;
}

std::vector<std::uint32_t> phrase_matches(const Query& query, const std::vector<TermIndex>& terms) {
    std::size_t anchor = 0;
    for (std::size_t i = 1; i < query.terms.size(); ++i) {
        if (terms[query.terms[i]].occurrences.size() < terms[query.terms[anchor]].occurrences.size())
            anchor = i;
    }
    std::vector<std::uint32_t> matches;
    const auto& occurrences = terms[query.terms[anchor]].occurrences;
    for (const Occurrence& occurrence : occurrences) {
        if (occurrence.position < anchor) continue;
        const auto start = static_cast<std::uint16_t>(occurrence.position - anchor);
        bool match = true;
        for (std::size_t i = 0; i < query.terms.size(); ++i) {
            if (!has_occurrence(terms[query.terms[i]], occurrence.document,
                                static_cast<std::uint16_t>(start + i))) {
                match = false;
                break;
            }
        }
        if (match && (matches.empty() || matches.back() != occurrence.document))
            matches.push_back(occurrence.document);
    }
    return matches;
}

void write_result(std::ostream& output, const std::vector<std::uint32_t>& ids) {
    std::uint64_t sum = 0;
    for (std::uint32_t id : ids) sum += id;
    output << ids.size() << ' ' << sum;
    for (std::size_t i = 0; i < ids.size() && i < 10; ++i) output << ' ' << ids[i];
    output << '\n';
}

int main() {
    std::ios::sync_with_stdio(false);
    
    TermMap ids;
    ids.reserve(150000);
    std::vector<TermIndex> terms;
    terms.reserve(150000);
    std::vector<Query> queries;
    {
        std::ifstream input("/data/queries.txt", std::ios::binary);
        if (!input) return 1;
        input.rdbuf()->pubsetbuf(nullptr, 256 * 1024);
        std::string line;
        line.reserve(256);
        while (std::getline(input, line)) {
            Query query;
            if (line.starts_with("BOOL ")) {
                const auto parts = split(std::string_view(line).substr(5));
                for (std::size_t i = 0; i < parts.size(); ++i) {
                    if (i % 2 == 0) query.terms.push_back(get_term(parts[i], ids, terms));
                    else query.operators.push_back(parts[i] == "AND" ? 'A' : parts[i] == "OR" ? 'O' : 'N');
                }
            } else if (line.starts_with("PHRASE \"") && line.back() == '"') {
                query.phrase = true;
                const auto parts = split(std::string_view(line).substr(8, line.size() - 9));
                for (auto part : parts) {
                    const auto id = get_term(part, ids, terms);
                    query.terms.push_back(id);
                    terms[id].needs_positions = true;
                }
            } else {
                return 1;
            }
            queries.push_back(std::move(query));
        }
        if (input.bad()) return 1;
    }

    {
        std::ifstream input("/data/docs.txt", std::ios::binary);
        if (!input) return 1;
        input.rdbuf()->pubsetbuf(nullptr, 512 * 1024);
        std::string line;
        line.reserve(1024);
        std::uint32_t document = 0;
        while (std::getline(input, line)) {
            const std::size_t tab = line.find('\t');
            if (tab == std::string::npos) return 1;
            std::string token;
            token.reserve(32);
            std::uint16_t position = 0;
            for (std::size_t i = tab + 1; i < line.size(); ++i) {
                unsigned char c = static_cast<unsigned char>(line[i]);
                if (c >= 'A' && c <= 'Z') c = static_cast<unsigned char>(c + ('a' - 'A'));
                if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
                    token.push_back(static_cast<char>(c));
                } else {
                    add_document_token(token, document, position, ids, terms);
                }
            }
            add_document_token(token, document, position, ids, terms);
            ++document;
        }
        if (input.bad()) return 1;
    }

    // Document postings are constructed in corpus order; no postprocessing pass.
    std::ofstream output("/out/results.txt", std::ios::binary);
    if (!output) return 1;
    output.rdbuf()->pubsetbuf(nullptr, 512 * 1024);
    std::vector<std::uint32_t> first, second;
    for (const Query& query : queries) {
        if (query.phrase) {
            write_result(output, phrase_matches(query, terms));
            continue;
        }
        const std::vector<std::uint32_t>* result = &terms[query.terms.front()].documents;
        bool use_first = true;
        for (std::size_t i = 0; i < query.operators.size(); ++i) {
            std::vector<std::uint32_t>& next = use_first ? first : second;
            combine(*result, terms[query.terms[i + 1]].documents, query.operators[i], next);
            result = &next;
            use_first = !use_first;
        }
        write_result(output, *result);
    }
    return output ? 0 : 1;
}
