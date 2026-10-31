#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <memory>
#include <malloc.h>
#include <queue>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>
#include <sys/mman.h>
#include <sys/stat.h>

using u32 = std::uint32_t;
#ifndef SD_TUNE_WORKERS
#define SD_TUNE_WORKERS 12
#endif
#ifndef SD_TUNE_PARAM
#define SD_TUNE_PARAM 32
#endif

using u64 = std::uint64_t;
constexpr u32 missing = UINT32_MAX;

u64 mix(u64 value) {
    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

class Transitions {
    struct Slot { u64 key = UINT64_MAX; u32 value = 0; };
    std::vector<Slot> slots_ = std::vector<Slot>(1024);
    std::size_t count_ = 0;
public:
    u32 find(u32 state, u32 word) const {
        const u64 key = (u64{state} << 32) | word;
        auto slot = mix(key) & (slots_.size() - 1);
        while (slots_[slot].key != UINT64_MAX) {
            if (slots_[slot].key == key) return slots_[slot].value;
            slot = (slot + 1) & (slots_.size() - 1);
        }
        return missing;
    }
    void insert(u32 state, u32 word, u32 value) {
        if ((count_ + 1) * 2 > slots_.size()) {
            auto old = std::move(slots_);
            slots_.resize(old.size() * 2);
            count_ = 0;
            for (const Slot& entry : old)
                if (entry.key != UINT64_MAX)
                    insert(static_cast<u32>(entry.key >> 32), static_cast<u32>(entry.key), entry.value);
        }
        const u64 key = (u64{state} << 32) | word;
        auto slot = mix(key) & (slots_.size() - 1);
        while (slots_[slot].key != UINT64_MAX) slot = (slot + 1) & (slots_.size() - 1);
        slots_[slot] = {key, value};
        ++count_;
    }
};

struct Query {
    u32 phrase = missing;
    std::vector<u32> words;
    std::vector<char> ops;
};
struct Matches {
    u64 sum = 0;
    u32 count = 0, last = missing;
    std::array<u32, 10> first{};
    void add(u32 document) {
        if (last == document) return;
        if (count < first.size()) first[count] = document;
        ++count;
        sum += document;
        last = document;
    }
    void merge(const Matches& source) {
        for (u32 i = 0; i < std::min<u32>(source.count, 10) && count + i < first.size(); ++i)
            first[count + i] = source.first[i];
        count += source.count;
        sum += source.sum;
    }
    std::string format() const {
        std::string result = std::to_string(count) + ' ' + std::to_string(sum);
        for (u32 i = 0; i < std::min<u32>(count, 10); ++i) result += ' ' + std::to_string(first[i]);
        return result + '\n';
    }
};
struct Posting {
    std::vector<u32> ids;
    std::unique_ptr<std::vector<u64>> bits;
    u32 base = 0;
};
struct Term {
    std::string text;
    u32 boolean = missing;
    Posting documents;
};
class Automaton {
    struct Node { u32 fail = 0, output = 0, edge = missing, query = missing; };
    struct Edge { u32 word, to, next; };
    std::vector<Node> nodes_{1};
    std::vector<Edge> edges_;
    std::vector<u32> next_query_;
    std::vector<u32> root_;
    Transitions transitions_;
    u32 transition(u32 state, u32 word) const {
        return state ? transitions_.find(state, word) : root_[word];
    }
public:
    Automaton(const std::vector<Query>& queries, std::size_t terms, u32 phrases)
        : next_query_(phrases, missing), root_(terms, missing) {
        for (const Query& query : queries) {
            if (query.phrase == missing) continue;
            const u32 q = query.phrase;
            u32 state = 0;
            for (u32 word : query.words) {
                u32 to = transition(state, word);
                if (to == missing) {
                    to = static_cast<u32>(nodes_.size());
                    nodes_.emplace_back();
                    if (state) transitions_.insert(state, word, to);
                    else root_[word] = to;
                    edges_.push_back({word, to, nodes_[state].edge});
                    nodes_[state].edge = static_cast<u32>(edges_.size() - 1);
                }
                state = to;
            }
            next_query_[q] = nodes_[state].query;
            nodes_[state].query = q;
        }
        std::queue<u32> queue;
        for (u32 e = nodes_[0].edge; e != missing; e = edges_[e].next) queue.push(edges_[e].to);
        while (!queue.empty()) {
            const u32 state = queue.front();
            queue.pop();
            for (u32 e = nodes_[state].edge; e != missing; e = edges_[e].next) {
                const Edge& edge = edges_[e];
                u32 fail = nodes_[state].fail;
                u32 to = transition(fail, edge.word);
                while (fail && to == missing) {
                    fail = nodes_[fail].fail;
                    to = transition(fail, edge.word);
                }
                Node& node = nodes_[edge.to];
                node.fail = to == missing ? 0 : to;
                node.output = nodes_[node.fail].query != missing ? node.fail : nodes_[node.fail].output;
                queue.push(edge.to);
            }
        }
        std::vector<Edge>().swap(edges_);
    }
    u32 advance(u32 state, u32 word, u32 document, std::vector<Matches>& matches) const {
        if (word == missing) return 0;
        u32 to = transition(state, word);
        while (state && to == missing) {
            state = nodes_[state].fail;
            to = transition(state, word);
        }
        state = to == missing ? 0 : to;
        for (u32 node = nodes_[state].query != missing ? state : nodes_[state].output; node; node = nodes_[node].output) {
            for (u32 q = nodes_[node].query; q != missing; q = next_query_[q]) {
                matches[q].add(document);
            }
        }
        return state;
    }
};

class Words {
    const std::vector<Term>& terms_;
    std::vector<u64> slots_;
public:
    explicit Words(const std::vector<Term>& terms) : terms_(terms) {
        std::size_t capacity = 1;
        while (capacity < terms.size() * 2 + 1) capacity *= 2;
        slots_.resize(capacity);
        for (u32 i = 0; i < terms.size(); ++i) {
            const u64 hash = std::hash<std::string_view>{}(terms[i].text);
            auto slot = hash & (capacity - 1);
            while (slots_[slot]) slot = (slot + 1) & (capacity - 1);
            slots_[slot] = (hash & 0xffffffff00000000ULL) | (u64{i} + 1);
        }
    }
    u32 find(std::string_view text) const {
        const u64 hash = std::hash<std::string_view>{}(text);
        auto slot = hash & (slots_.size() - 1);
        while (slots_[slot]) {
            const u32 id = static_cast<u32>(slots_[slot]) - 1;
            if ((slots_[slot] >> 32) == (hash >> 32) && terms_[id].text == text) return id;
            slot = (slot + 1) & (slots_.size() - 1);
        }
        return missing;
    }
};

template<class Work>
void parallel(unsigned workers, Work work) {
    std::vector<std::exception_ptr> errors(workers);
    std::vector<std::jthread> threads;
    for (unsigned w = 0; w < workers; ++w)
        threads.emplace_back([&, w] {
            try { work(w); } catch (...) { errors[w] = std::current_exception(); }
        });
    threads.clear();
    for (auto error : errors) if (error) std::rethrow_exception(error);
}

struct Local {
    struct Hit { u32 term, document; };
    std::vector<Hit> hits;
    std::unique_ptr<u32[]> documents;
    std::vector<u32> offsets;
    std::vector<Matches> matches;
    u32 extent = 0;
    std::span<const u32> posting(u32 term) const {
        return {documents.get() + offsets[term], offsets[term + 1] - offsets[term]};
    }
};

void scan(Local& local, const char* data, std::size_t begin, std::size_t end, const Words& words,
          const std::vector<Term>& terms, const Automaton& automaton, std::size_t queries, u32 booleans) {
    local.offsets.resize(static_cast<std::size_t>(booleans) + 1);
    local.matches.resize(queries);
    std::vector<u32> last(booleans, missing);
    if (begin && data[begin - 1] != '\n')
        while (data[begin++] != '\n') {}
    if (end && data[end - 1] != '\n')
        while (data[end++] != '\n') {}
    local.hits.reserve((end - begin) / 12 + 1);
    const char* p = data + begin;
    auto released = (begin + 4095) & ~std::size_t(4095);
    while (p < data + end) {
        const char* stop = static_cast<const char*>(std::memchr(p, '\n', data + end - p));
        if (!stop) throw std::runtime_error("unterminated document");
        u32 document = 0;
        while (*p >= '0' && *p <= '9') document = document * 10 + static_cast<unsigned>(*p++ - '0');
        if (*p++ != '\t') throw std::runtime_error("invalid document id");
        local.extent = document + 1;
        u32 state = 0;
        while (p < stop) {
            char token[32];
            std::size_t size = 0;
            while (p < stop) {
                const unsigned c = static_cast<unsigned char>(*p) | 32;
                if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) break;
                if (size < sizeof(token)) token[size] = static_cast<char>(c);
                ++size;
                ++p;
            }
            if (size) {
                const u32 word = size <= sizeof(token) ? words.find({token, size}) : missing;
                if (word != missing && terms[word].boolean != missing) {
                    const u32 index = terms[word].boolean;
                    if (last[index] != document) {
                        local.hits.push_back({index, document});
                        ++local.offsets[index + 1];
                        last[index] = document;
                    }
                }
                state = automaton.advance(state, word, document, local.matches);
            }
            if (p < stop) ++p;
        }
        p = stop + 1;
        const auto consumed = static_cast<std::size_t>(p - data);
        if (consumed > released && consumed - released >= 1024 * 1024) {
            const auto stop = consumed & ~std::size_t(4095);
            if (madvise(const_cast<char*>(data) + released, stop - released, MADV_DONTNEED))
                throw std::runtime_error("cannot release consumed document pages");
            released = stop;
        }
    }
}

std::string format_bits(const std::vector<u64>& bits) {
    u64 sum = 0, count = 0;
    std::array<u32, 10> first{};
    for (std::size_t w = 0; w < bits.size(); ++w) {
        u64 word = bits[w];
        const auto population = static_cast<u32>(std::popcount(word));
        if (count < first.size()) {
            u64 remaining = word;
            for (u32 i = 0; remaining && count + i < first.size(); ++i) {
                first[count + i] = static_cast<u32>(w * 64 + std::countr_zero(remaining));
                remaining &= remaining - 1;
            }
        }
        count += population;
        sum += w * 64 * population;
        if (population < 8) {
            while (word) {
                sum += std::countr_zero(word);
                word &= word - 1;
            }
        } else {
            sum += std::popcount(word & 0xaaaaaaaaaaaaaaaaULL)
                 + 2ULL * std::popcount(word & 0xccccccccccccccccULL)
                 + 4ULL * std::popcount(word & 0xf0f0f0f0f0f0f0f0ULL)
                 + 8ULL * std::popcount(word & 0xff00ff00ff00ff00ULL)
                 + 16ULL * std::popcount(word & 0xffff0000ffff0000ULL)
                 + 32ULL * std::popcount(word & 0xffffffff00000000ULL);
        }
    }
    std::string result = std::to_string(count) + ' ' + std::to_string(sum);
    for (std::size_t i = 0; i < std::min<u64>(10, count); ++i) result += ' ' + std::to_string(first[i]);
    return result + '\n';
}

void apply(std::vector<u64>& bits, const Posting& posting, char op, std::vector<u64>& scratch) {
    if (!posting.bits) {
        if (op == 'A') {
            std::fill(scratch.begin(), scratch.end(), 0);
            for (u32 id : posting.ids) scratch[id / 64] |= bits[id / 64] & (u64{1} << (id % 64));
            bits.swap(scratch);
        } else for (u32 id : posting.ids) {
            if (op == 'O') bits[id / 64] |= u64{1} << (id % 64);
            else bits[id / 64] &= ~(u64{1} << (id % 64));
        }
    } else {
        const auto start = posting.base / 64;
        const auto stop = start + posting.bits->size();
        if (op == 'A') {
            std::fill(bits.begin(), bits.begin() + start, 0);
            std::fill(bits.begin() + stop, bits.end(), 0);
        }
        for (std::size_t i = start; i < stop; ++i) {
            if (op == 'A') bits[i] &= (*posting.bits)[i - start];
            else if (op == 'O') bits[i] |= (*posting.bits)[i - start];
            else bits[i] &= ~(*posting.bits)[i - start];
        }
    }
}

int solve() {
#if !defined(__SANITIZE_ADDRESS__)
    if (!mallopt(M_ARENA_MAX, 12)) throw std::runtime_error("cannot set allocator arena limit");
#endif
    std::vector<Query> queries;
    std::vector<Term> terms;
    std::unordered_map<std::string, u32> ids;
    std::vector<u32> boolean_terms;
    u32 booleans = 0, phrases = 0;
    std::ifstream input("/data/queries.txt");
    if (!input) throw std::runtime_error("cannot open queries.txt");
    std::string line;
    while (std::getline(input, line)) {
        Query query;
        const bool phrase = line.starts_with("PHRASE \"");
        if (phrase) query.phrase = phrases++;
        std::string_view text(line);
        if (phrase && text.back() == '"') { text.remove_prefix(8); text.remove_suffix(1); }
        else if (text.starts_with("BOOL ")) text.remove_prefix(5);
        else throw std::runtime_error("invalid query");
        unsigned part = 0;
        while (!text.empty()) {
            const auto space = text.find(' ');
            const std::string word(text.substr(0, space));
            if (!phrase && part % 2) {
                if (word != "AND" && word != "OR" && word != "NOT") throw std::runtime_error("invalid operator");
                query.ops.push_back(word.front());
            } else {
                auto [it, fresh] = ids.try_emplace(word, static_cast<u32>(terms.size()));
                if (fresh) terms.push_back({word, missing, {}});
                query.words.push_back(it->second);
                if (!phrase && terms[it->second].boolean == missing) {
                    terms[it->second].boolean = booleans++;
                    boolean_terms.push_back(it->second);
                }
            }
            ++part;
            if (space == text.npos) break;
            text.remove_prefix(space + 1);
        }
        if (query.words.empty()) throw std::runtime_error("empty query");
        queries.push_back(std::move(query));
    }
    if (input.bad()) throw std::runtime_error("cannot read queries.txt");
    ids.clear();
    ids.rehash(0);
    const Words words(terms);
    const Automaton automaton(queries, terms.size(), phrases);
    struct stat status{};
    if (stat("/data/docs.txt", &status)) throw std::runtime_error("cannot stat docs.txt");
    const auto size = static_cast<std::size_t>(status.st_size);
    auto close_file = [](FILE* file) { if (file) std::fclose(file); };
    std::unique_ptr<FILE, decltype(close_file)> corpus(std::fopen("/data/docs.txt", "rb"), close_file);
    if (!corpus) throw std::runtime_error("cannot open docs.txt");
    auto unmap = [size](void* p) { if (p) munmap(p, size); };
    std::unique_ptr<void, decltype(unmap)> mapping(nullptr, unmap);
    if (size) {
        void* p = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fileno(corpus.get()), 0);
        if (p == MAP_FAILED) throw std::runtime_error("cannot map docs.txt");
        mapping.reset(p);
    }
    const char* data = size ? static_cast<const char*>(mapping.get()) : "";
    const unsigned workers = size < 2 * 1024 * 1024 ? 1 : SD_TUNE_WORKERS;
    std::vector<Local> locals(workers);
    parallel(workers, [&](unsigned w) {
        scan(locals[w], data, size * w / workers, size * (w + 1) / workers, words, terms, automaton, phrases, booleans);
    });
    parallel(workers, [&](unsigned w) {
        Local& local = locals[w];
        for (std::size_t i = 1; i < local.offsets.size(); ++i)
            local.offsets[i] += local.offsets[i - 1];
        local.documents = std::make_unique_for_overwrite<u32[]>(local.hits.size());
        auto cursors = local.offsets;
        for (const Local::Hit& hit : local.hits)
            local.documents[cursors[hit.term]++] = hit.document;
        std::vector<Local::Hit>().swap(local.hits);
    });
    std::vector<Matches> matches(phrases);
    u32 extent = 0;
    for (Local& local : locals) {
        extent = std::max(extent, local.extent);
        for (u32 q = 0; q < phrases; ++q) matches[q].merge(local.matches[q]);
    }
    std::atomic<u32> next_term{0};
    parallel(workers, [&](unsigned) {
      for (;;) {
        const u32 begin = next_term.fetch_add(SD_TUNE_PARAM, std::memory_order_relaxed);
        if (begin >= booleans) break;
        for (u32 b = begin; b < std::min<u32>(begin + SD_TUNE_PARAM, booleans); ++b) {
            std::size_t count = 0;
            u32 first = missing, last = 0;
            for (const Local& local : locals) {
                const auto ids = local.posting(b);
                count += ids.size();
                if (!ids.empty()) {
                    if (first == missing) first = ids.front();
                    last = ids.back();
                }
            }
            if (!count) continue;
            Posting& posting = terms[boolean_terms[b]].documents;
            const bool dense = count >= 256 && last - first < count * 32;
            if (dense) {
                posting.base = first & ~u32{63};
                posting.bits = std::make_unique<std::vector<u64>>((last - posting.base) / 64 + 1);
            } else posting.ids.reserve(count);
            for (const Local& local : locals) {
                const auto ids = local.posting(b);
                if (dense) {
                    for (u32 id : ids)
                        (*posting.bits)[(id - posting.base) / 64] |= u64{1} << (id % 64);
                } else posting.ids.insert(posting.ids.end(), ids.begin(), ids.end());
            }
        }
      }
    });
    parallel(workers, [&](unsigned w) { locals[w] = Local{}; });
    std::vector<std::string> results(queries.size());
    std::atomic<std::size_t> next{0};
    parallel(workers, [&](unsigned) {
        std::vector<u64> first((static_cast<std::size_t>(extent) + 63) / 64), second(first.size());
        for (;;) {
            const auto q = next.fetch_add(1, std::memory_order_relaxed);
            if (q >= queries.size()) break;
            const Query& query = queries[q];
            if (query.phrase != missing) { results[q] = matches[query.phrase].format(); continue; }
            std::fill(first.begin(), first.end(), 0);
            apply(first, terms[query.words[0]].documents, 'O', second);
            for (std::size_t i = 0; i < query.ops.size(); ++i) {
                apply(first, terms[query.words[i + 1]].documents, query.ops[i], second);
            }
            results[q] = format_bits(first);
        }
    });
    std::ofstream output("/out/results.txt", std::ios::binary);
    if (!output) throw std::runtime_error("cannot open results.txt");
    for (const auto& result : results) output << result;
    output.close();
    if (!output) throw std::runtime_error("cannot write results.txt");
    return 0;
}

int main() {
    try { return solve(); }
    catch (const std::exception& error) { std::fprintf(stderr, "track-b: %s\n", error.what()); return 1; }
}
