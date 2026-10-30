#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <variant>

#include "lab.hpp"

namespace lab {
namespace {
char lowerAscii(char c) {
  return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
}

bool tokenCharacter(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

template <class Visit>
void visitTokens(std::string_view text, Visit visit) {
  std::size_t start = 0;
  for (std::size_t i = 0; i <= text.size(); ++i) {
    if (i < text.size() && tokenCharacter(text[i]))
      continue;
    if (i > start)
      visit(text.substr(start, i - start));
    start = i + 1;
  }
}

std::vector<std::string> tokens(std::string_view text) {
  std::vector<std::string> result;
  visitTokens(text, [&](std::string_view token) {
    std::string normalized;
    normalized.reserve(token.size());
    for (const char c : token)
      normalized.push_back(lowerAscii(c));
    result.push_back(std::move(normalized));
  });
  return result;
}
struct ParsedQuery {
  bool phrase;
  std::vector<std::string> words, operators;
};

ParsedQuery parseQuery(const std::string& line) {
  ParsedQuery query{};
  if (line.starts_with("BOOL ")) {
    const auto parts = fields(std::string_view(line).substr(5));
    if (parts.empty() || parts.size() % 2 == 0)
      throw std::runtime_error("invalid practice boolean query");
    for (std::size_t i = 0; i < parts.size(); ++i) {
      if (i % 2 == 0) {
        auto terms = tokens(parts[i]);
        if (terms.size() != 1)
          throw std::runtime_error("boolean operand must be one token");
        query.words.push_back(std::move(terms.front()));
      } else {
        if (parts[i] != "AND" && parts[i] != "OR" && parts[i] != "NOT")
          throw std::runtime_error("unknown boolean operator");
        query.operators.emplace_back(parts[i]);
      }
    }
  } else if (line.starts_with("PHRASE \"") && line.size() >= 9 && line.back() == '"') {
    query.phrase = true;
    query.words = tokens(std::string_view(line).substr(8, line.size() - 9));
    if (query.words.empty())
      throw std::runtime_error("empty practice phrase");
  } else
    throw std::runtime_error("unknown practice query kind");
  return query;
}
}

namespace {
void indexCompact(const std::filesystem::path& corpus, const std::filesystem::path& queries,
                  const std::filesystem::path& output, IndexMetrics* metrics, std::size_t workers);
}

void index(const std::filesystem::path& corpus, const std::filesystem::path& queries,
           const std::filesystem::path& output, IndexMetrics* metrics, std::size_t workers,
           std::string_view algorithm) {
  if (algorithm.empty())
    algorithm = "compact";
  if (algorithm != "compact")
    throw std::runtime_error("B algorithm must be compact");
  indexCompact(corpus, queries, output, metrics, workers);
}

namespace {
constexpr std::uint32_t kMissing = std::numeric_limits<std::uint32_t>::max();

using DocumentPostings = std::variant<std::vector<std::uint16_t>, std::vector<std::uint32_t>>;

struct CompactTerm {
  DocumentPostings documents;
  std::uint32_t postings = 0;
};

struct TermHash {
  using is_transparent = void;
  std::size_t operator()(std::string_view word) const {
    std::uint64_t head = 0;
    const auto n = word.size() < 8 ? word.size() : 8;
    if (n)
      std::memcpy(&head, word.data(), n);
    std::uint64_t tail = 0;
    if (word.size() > 8)
      std::memcpy(&tail, word.data() + word.size() - 8, 8);
    head |= 0x2020202020202020ull;
    tail |= 0x2020202020202020ull;
    head ^= tail * 0x9E3779B97F4A7C15ull;
    head *= 0xBF58476D1CE4E5B9ull;
    return static_cast<std::size_t>(head);
  }
};

struct TermEqual {
  using is_transparent = void;
  bool operator()(std::string_view left, std::string_view right) const {
    if (left.size() != right.size())
      return false;
    for (std::size_t i = 0; i < left.size(); ++i)
      if (lowerAscii(left[i]) != lowerAscii(right[i]))
        return false;
    return true;
  }
};

using TermIds = std::unordered_map<std::string, std::uint32_t, TermHash, TermEqual>;

struct CompactIndex {
  const TermIds& ids;
  const std::unordered_set<std::uint64_t>& wanted;
  std::vector<CompactTerm> terms;
  std::vector<std::uint32_t> head, touched;
  std::uint64_t documents = 0;
  std::vector<std::uint64_t> filter = std::vector<std::uint64_t>(1u << 14, 0);
  std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> bigrams;
  std::uint32_t current_document_ = 0, last_position_ = 0, last_term_ = 0;
  bool has_last_ = false, wide_documents_ = false;
  std::vector<std::vector<std::uint32_t>> buckets;
  bool packing_ok_ = true;

  static std::uint64_t pairKey(std::uint32_t left, std::uint32_t right) {
    return (static_cast<std::uint64_t>(left) << 32) | right;
  }

  static std::uint32_t filterHash(std::string_view word) {
    std::uint64_t head = 0;
    const auto n = word.size() < 8 ? word.size() : 8;
    if (n)
      std::memcpy(&head, word.data(), n);
    head |= 0x2020202020202020ull;
    head *= 0xBF58476D1CE4E5B9ull;
    head ^= head >> 29;
    return static_cast<std::uint32_t>(head) & ((1u << 20) - 1);
  }

  CompactIndex(const TermIds& query_ids, const std::unordered_set<std::uint64_t>& pairs)
      : ids(query_ids), wanted(pairs), terms(query_ids.size()), head(query_ids.size(), kMissing) {
    buckets.assign((query_ids.size() + 255) / 256, {});
    for (const auto& [word, id] : query_ids) {
      (void)id;
      const auto bit = filterHash(word);
      filter[bit >> 6] |= std::uint64_t{1} << (bit & 63);
    }
  }

  void promoteDocuments() {
    for (auto& term : terms) {
      auto& narrow = std::get<std::vector<std::uint16_t>>(term.documents);
      std::vector<std::uint32_t> wide(narrow.begin(), narrow.end());
      term.documents = std::move(wide);
    }
    wide_documents_ = true;
  }

  void beginDocument(std::uint32_t document) {
    if (packing_ok_ && document >= (1u << 17)) {
      flushBuckets();
      packing_ok_ = false;
    }
    if (packing_ok_ && document && document % 4096 == 0)
      flushBuckets();
    if (document > std::numeric_limits<std::uint16_t>::max() && !wide_documents_)
      promoteDocuments();
    current_document_ = document;
    has_last_ = false;
    touched.clear();
  }

  void addToken(std::string_view word, std::uint32_t position) {
    const auto bit = filterHash(word);
    if ((filter[bit >> 6] & (std::uint64_t{1} << (bit & 63))) == 0)
      return;
    const auto found = ids.find(word);
    if (found == ids.end())
      return;
    const auto id = found->second;
    if (head[id] == kMissing) {
      touched.push_back(id);
      head[id] = 0;
    }
    if (has_last_ && position == last_position_ + 1) {
      const auto key = pairKey(last_term_, id);
      if (wanted.count(key))
        bigrams[key].push_back((std::uint64_t{current_document_} << 32) | last_position_);
    }
    last_term_ = id;
    last_position_ = position;
    has_last_ = true;
  }

  void flushBuckets() {
    std::vector<std::uint32_t> ordered;
    for (auto& entries : buckets) {
      if (entries.empty())
        continue;
      std::array<std::uint32_t, 256> counts{};
      for (const auto entry : entries)
        ++counts[entry >> 17];
      std::array<std::uint32_t, 256> start{};
      std::uint32_t run = 0;
      for (std::size_t offset = 0; offset < 256; ++offset) {
        start[offset] = run;
        run += counts[offset];
      }
      ordered.resize(entries.size());
      for (const auto entry : entries)
        ordered[start[entry >> 17]++] = entry & 0x1FFFFu;
      std::size_t at = 0;
      const auto base = static_cast<std::size_t>(&entries - buckets.data()) * 256;
      for (std::size_t offset = 0; offset < 256; ++offset) {
        const auto count = counts[offset];
        if (!count)
          continue;
        auto& term = terms[base + offset];
        const auto* run_begin = ordered.data() + at;
        std::uint32_t widest = 0;
        for (std::uint32_t k = 0; k < count; ++k)
          widest = std::max(widest, run_begin[k]);
        if (!wide_documents_ && widest > 65535)
          promoteDocuments();
        if (wide_documents_) {
          auto& list = std::get<std::vector<std::uint32_t>>(term.documents);
          list.insert(list.end(), run_begin, run_begin + count);
        } else {
          auto& list = std::get<std::vector<std::uint16_t>>(term.documents);
          for (std::uint32_t k = 0; k < count; ++k)
            list.push_back(static_cast<std::uint16_t>(run_begin[k]));
        }
        at += count;
      }
      std::vector<std::uint32_t>().swap(entries);
    }
  }

  void finishDocument(std::uint32_t document) {
    for (const auto id : touched) {
      auto& term = terms[id];
      if (packing_ok_) {
        buckets[static_cast<std::size_t>(id >> 8)].push_back(
            (static_cast<std::uint32_t>(id & 0xFF) << 17) | document);
      } else if (wide_documents_) {
        std::get<std::vector<std::uint32_t>>(term.documents).push_back(document);
      } else {
        std::get<std::vector<std::uint16_t>>(term.documents)
            .push_back(static_cast<std::uint16_t>(document));
      }
      ++term.postings;
      head[id] = kMissing;
    }
  }

  const CompactTerm* find(std::string_view word) const {
    const auto found = ids.find(word);
    return found == ids.end() ? nullptr : &terms[found->second];
  }
};

void intersectInto(std::vector<std::uint32_t>& left, const std::vector<std::uint32_t>& right) {
  if (left.empty())
    return;
  std::vector<std::uint32_t> merged;
  if (left.size() * 16 < right.size()) {
    merged.reserve(left.size());
    for (const auto doc : left)
      if (std::binary_search(right.begin(), right.end(), doc))
        merged.push_back(doc);
  } else {
    merged.reserve(std::min(left.size(), right.size()));
    auto a = left.begin();
    auto b = right.begin();
    while (a != left.end() && b != right.end()) {
      if (*a < *b) {
        ++a;
      } else if (*b < *a) {
        ++b;
      } else {
        merged.push_back(*a);
        ++a;
        ++b;
      }
    }
  }
  left = std::move(merged);
}

void unionInto(std::vector<std::uint32_t>& left, const std::vector<std::uint32_t>& right) {
  std::vector<std::uint32_t> merged;
  merged.reserve(left.size() + right.size());
  auto a = left.begin();
  auto b = right.begin();
  while (a != left.end() && b != right.end()) {
    if (*a < *b)
      merged.push_back(*a++);
    else if (*b < *a)
      merged.push_back(*b++);
    else {
      merged.push_back(*a);
      ++a;
      ++b;
    }
  }
  merged.insert(merged.end(), a, left.end());
  merged.insert(merged.end(), b, right.end());
  left = std::move(merged);
}

void subtractInto(std::vector<std::uint32_t>& left, const std::vector<std::uint32_t>& right) {
  std::vector<std::uint32_t> kept;
  kept.reserve(left.size());
  auto b = right.begin();
  for (const auto doc : left) {
    while (b != right.end() && *b < doc)
      ++b;
    if (b == right.end() || *b != doc)
      kept.push_back(doc);
  }
  left = std::move(kept);
}

std::string evaluateCompact(const CompactIndex& idx, const ParsedQuery& query,
                            std::vector<std::uint32_t>& scratch, std::uint64_t document_base) {
  std::vector<std::uint32_t> result;
  auto decode = [&](const std::string& word, std::vector<std::uint32_t>& out) {
    const auto* term = idx.find(word);
    if (term)
      std::visit([&](const auto& documents) { out.assign(documents.begin(), documents.end()); },
                 term->documents);
    else
      out.clear();
  };
  const auto frequency = [&](const std::string& word) {
    const auto* term = idx.find(word);
    return term ? term->postings : 0U;
  };

  if (!query.phrase) {
    std::size_t chain = 0;
    while (chain < query.operators.size() && query.operators[chain] == "AND")
      ++chain;
    std::size_t pivot = 0;
    for (std::size_t i = 1; i <= chain; ++i)
      if (frequency(query.words[i]) < frequency(query.words[pivot]))
        pivot = i;
    decode(query.words[pivot], result);
    for (std::size_t i = 0; i < query.operators.size(); ++i) {
      if (result.empty() && query.operators[i] != "OR")
        continue;
      decode(query.words[i + 1 == pivot ? 0 : i + 1], scratch);
      if (query.operators[i] == "AND")
        intersectInto(result, scratch);
      else if (query.operators[i] == "OR")
        unionInto(result, scratch);
      else
        subtractInto(result, scratch);
    }
  } else if (query.words.size() == 1) {
    decode(query.words.front(), result);
  } else {
    const auto count = query.words.size();
    std::vector<const std::vector<std::uint64_t>*> lists(count - 1, nullptr);
    std::size_t smallest = 0;
    bool complete = true;
    for (std::size_t i = 1; i < count; ++i) {
      const auto found = idx.bigrams.find(
          CompactIndex::pairKey(idx.ids.at(query.words[i - 1]), idx.ids.at(query.words[i])));
      if (found == idx.bigrams.end() || found->second.empty()) {
        complete = false;
        break;
      }
      lists[i - 1] = &found->second;
      if (found->second.size() < lists[smallest]->size())
        smallest = i - 1;
    }
    if (complete) {
      for (const auto entry : *lists[smallest]) {
        const auto document = static_cast<std::uint32_t>(entry >> 32);
        const auto position = static_cast<std::uint32_t>(entry);
        if (position < smallest)
          continue;
        const auto start = position - static_cast<std::uint32_t>(smallest);
        bool match = true;
        for (std::size_t i = 0; i < count - 1 && match; ++i) {
          if (i == smallest)
            continue;
          const auto want = (static_cast<std::uint64_t>(document) << 32) |
                            (start + static_cast<std::uint32_t>(i));
          match = std::binary_search(lists[i]->begin(), lists[i]->end(), want);
        }
        if (match && (result.empty() || result.back() != document))
          result.push_back(document);
      }
    }
  }

  std::string row;
  row.reserve(result.size() * 6 + 1);
  for (std::size_t i = 0; i < result.size(); ++i) {
    if (i)
      row += ',';
    row += std::to_string(document_base + result[i]);
  }
  row += '\n';
  return row;
}

void indexCompact(const std::filesystem::path& corpus, const std::filesystem::path& queries,
                  const std::filesystem::path& output, IndexMetrics* metrics, std::size_t workers) {
  using Clock = std::chrono::steady_clock;
  const auto build_start = metrics ? Clock::now() : Clock::time_point{};

  std::ifstream query_file(queries, std::ios::binary);
  if (!query_file)
    throw std::runtime_error("cannot open queries: " + queries.string());
  std::vector<ParsedQuery> queries_parsed;
  TermIds query_ids;
  std::unordered_set<std::uint64_t> phrase_pairs;
  {
    std::string line;
    while (std::getline(query_file, line)) {
      if (!line.empty() && line.back() == '\r')
        line.pop_back();
      auto parsed = parseQuery(line);
      for (const auto& word : parsed.words) {
        if (query_ids.size() >= kMissing)
          throw std::runtime_error("too many query terms");
        query_ids.try_emplace(word, static_cast<std::uint32_t>(query_ids.size()));
      }
      if (parsed.phrase)
        for (std::size_t i = 1; i < parsed.words.size(); ++i)
          phrase_pairs.insert(CompactIndex::pairKey(query_ids.at(parsed.words[i - 1]),
                                                    query_ids.at(parsed.words[i])));
      queries_parsed.push_back(std::move(parsed));
    }
    if (query_file.bad())
      throw std::runtime_error("failed reading queries");
  }

  constexpr std::size_t kShards = 12;
  const auto file_size = static_cast<std::size_t>(std::filesystem::file_size(corpus));
  std::vector<std::size_t> bounds(kShards + 1, 0);
  {
    std::ifstream probe(corpus, std::ios::binary);
    if (!probe)
      throw std::runtime_error("cannot open corpus: " + corpus.string());
    for (std::size_t i = 1; i < kShards; ++i) {
      auto pos = file_size * i / kShards;
      pos = std::max(pos, bounds[i - 1]);
      probe.seekg(static_cast<std::streamoff>(pos));
      bool found = false;
      for (std::size_t step = 0; step < (1u << 20) && pos < file_size; ++step) {
        const auto byte = probe.get();
        if (byte == std::char_traits<char>::eof())
          break;
        ++pos;
        if (byte == '\n') {
          found = true;
          break;
        }
      }
      bounds[i] = found ? pos : file_size;
    }
    bounds[kShards] = file_size;
  }

  std::vector<std::unique_ptr<CompactIndex>> shards;
  shards.reserve(kShards);
  for (std::size_t s = 0; s < kShards; ++s)
    shards.push_back(std::make_unique<CompactIndex>(query_ids, phrase_pairs));
  practice::parallelRanges(kShards, workers, [&](std::size_t begin, std::size_t end) {
    for (std::size_t s = begin; s < end; ++s) {
      auto& idx = *shards[s];
      std::ifstream documents(corpus, std::ios::binary);
      if (!documents)
        throw std::runtime_error("cannot open corpus: " + corpus.string());
      documents.seekg(static_cast<std::streamoff>(bounds[s]));
      const auto limit = bounds[s + 1] - bounds[s];
      std::uint64_t consumed = 0, document = 0;
      std::uint32_t position = 0;
      std::vector<char> window(1u << 20);
      std::size_t carry = 0;
      const auto handle_line = [&](const char* start, std::size_t length) {
        if (document > std::numeric_limits<std::uint32_t>::max())
          throw std::runtime_error("too many corpus documents");
        const auto local_document = static_cast<std::uint32_t>(document);
        idx.beginDocument(local_document);
        visitTokens(std::string_view(start, length), [&](std::string_view word) {
          if (position == std::numeric_limits<std::uint32_t>::max())
            throw std::runtime_error("too many positions in one document");
          idx.addToken(word, position++);
        });
        idx.finishDocument(local_document);
        ++document;
        position = 0;
      };
      while (consumed < limit) {
        if (carry == window.size())
          window.resize(window.size() * 2);
        const auto want = std::min<std::uint64_t>(window.size() - carry, limit - consumed);
        documents.read(window.data() + carry, static_cast<std::streamsize>(want));
        const auto got = static_cast<std::size_t>(documents.gcount());
        if (got == 0)
          break;
        consumed += got;
        const auto total = carry + got;
        std::size_t start = 0;
        for (std::size_t i = 0; i < total; ++i) {
          if (window[i] != '\n')
            continue;
          handle_line(window.data() + start, i - start);
          start = i + 1;
        }
        carry = total - start;
        std::memmove(window.data(), window.data() + start, carry);
      }
      if (consumed != limit || documents.bad())
        throw std::runtime_error("failed reading corpus");
      if (carry != 0)
        handle_line(window.data(), carry);
      idx.flushBuckets();
      idx.documents = document;
    }
  });
  std::vector<std::uint64_t> document_start(kShards + 1, 0);
  for (std::size_t s = 0; s < kShards; ++s) {
    document_start[s + 1] = document_start[s] + shards[s]->documents;
    if (document_start[s + 1] >
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) + 1)
      throw std::runtime_error("too many corpus documents");
  }
  const auto query_start = metrics ? Clock::now() : Clock::time_point{};

  std::ofstream out(output, std::ios::binary);
  if (!out)
    throw std::runtime_error("cannot open output: " + output.string());
  constexpr std::size_t kQueryBatch = 4096;
  for (std::size_t base = 0; base < queries_parsed.size(); base += kQueryBatch) {
    const auto count = std::min(kQueryBatch, queries_parsed.size() - base);
    std::vector<std::string> rows(count);
    practice::parallelRanges(count, workers, [&](std::size_t begin, std::size_t end) {
      std::vector<std::uint32_t> scratch;
      for (std::size_t i = begin; i < end; ++i) {
        std::string row;
        for (std::size_t s = 0; s < shards.size(); ++s) {
          std::string part =
              evaluateCompact(*shards[s], queries_parsed[base + i], scratch, document_start[s]);
          if (!part.empty() && part.back() == '\n')
            part.pop_back();
          if (part.empty())
            continue;
          if (!row.empty())
            row.push_back(',');
          row += part;
        }
        row.push_back('\n');
        rows[i] = std::move(row);
      }
    });
    for (const auto& row : rows)
      out << row;
  }
  out.close();
  if (!out)
    throw std::runtime_error("failed writing output: " + output.string());
  if (metrics) {
    *metrics = {};
    metrics->build_seconds = std::chrono::duration<double>(query_start - build_start).count();
    metrics->query_seconds = std::chrono::duration<double>(Clock::now() - query_start).count();
    metrics->worker_threads = std::max<std::size_t>(1, workers);
    for (std::size_t id = 0; id < query_ids.size(); ++id)
      if (std::any_of(shards.begin(), shards.end(),
                      [id](const auto& shard) { return shard->terms[id].postings != 0; }))
        ++metrics->terms;
    for (const auto& shard : shards) {
      metrics->documents += shard->documents;
      for (const auto& term : shard->terms) {
        metrics->postings += term.postings;
        std::visit(
            [&](const auto& documents) {
              const auto bytes = sizeof(documents.front());
              metrics->posting_capacity_bytes += documents.size() * bytes;
              metrics->posting_bytes = std::max(metrics->posting_bytes, bytes);
            },
            term.documents);
      }
      metrics->posting_capacity_bytes += shard->terms.size() * sizeof(CompactTerm);
    }
    if (metrics->posting_bytes == 0)
      metrics->posting_bytes = sizeof(std::uint16_t);
    metrics->position_capacity_bytes = 0;
  }
}
}
}
