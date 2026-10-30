#include "graph.hpp"
#include "metrics.hpp"
#include "parallel_ranges.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <tuple>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace practice {
namespace fs = std::filesystem;

class TokenStream { std::string_view text_; std::size_t pos_ = 0;

public:
    explicit TokenStream(std::string_view text) : text_(text) {}
    std::size_t position() const { return pos_; }
    static bool space(char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f'; }
    bool next(std::uint64_t& value) { while (pos_ < text_.size() && space(text_[pos_])) ++pos_;
        if (pos_ == text_.size()) return false; std::uint64_t parsed = 0; std::size_t digits = 0;
        while (pos_ < text_.size()) { const char c = text_[pos_]; if (c < '0' || c > '9') break;
            const auto digit = static_cast<std::uint64_t>(c - '0');
            if (parsed > (std::numeric_limits<std::uint64_t>::max() - digit) / 10)
                throw std::runtime_error("integer token overflow");
            parsed = parsed * 10 + digit; ++digits;
            ++pos_; }
        if (digits == 0) throw std::runtime_error("invalid integer token"); value = parsed;
        return true; } };

std::string readTextFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate); if (!in) throw std::runtime_error("cannot open input: " + path.string());
    const auto size = in.tellg(); if (size < 0) throw std::runtime_error("cannot size input: " + path.string()); std::string text(static_cast<std::size_t>(size), '\0'); in.seekg(0);
    if (!in.read(text.data(), static_cast<std::streamsize>(text.size())))
        throw std::runtime_error("failed reading input: " + path.string());
    return text; }

bool parseU32(const char*& p, const char* stop, std::uint32_t& out) {
    while (p < stop && (*p == ' ' || *p == '\t')) ++p; if (p == stop) return false; std::uint64_t value = 0; std::size_t digits = 0;
    while (p < stop) { const char c = *p; if (c < '0' || c > '9') break;
        value = value * 10 + static_cast<std::uint64_t>(c - '0'); if (value > 0xFFFFFFFFULL) return false; ++digits;
        ++p; }
    if (digits == 0) return false; out = static_cast<std::uint32_t>(value);
    return true; }

template <class Visit>
void parseEdgeLine(std::string_view line, std::uint32_t n, Visit&& visit) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.remove_suffix(1);
    if (line.empty()) return; const char* p = line.data(); const char* const stop = p + line.size(); Vertex u = 0, v = 0; Weight w = 0;
    if (!parseU32(p, stop, u) || !parseU32(p, stop, v) || !parseU32(p, stop, w))
        throw std::runtime_error("edge line must be three decimals");
    if (u >= n || v >= n) throw std::runtime_error("edge endpoint outside node range"); while (p < stop && (*p == ' ' || *p == '\t')) ++p;
    if (p != stop) throw std::runtime_error("unexpected edge line layout"); visit(u, v, w); }

template <class Visit>
void forEachEdgeChunk(const fs::path& path, std::size_t begin, std::size_t end, std::uint32_t n,
                      Visit&& visit) {
    std::ifstream in(path, std::ios::binary); if (!in) throw std::runtime_error("cannot open graph: " + path.string());
    in.seekg(static_cast<std::streamoff>(begin)); constexpr std::size_t kWindow = 256 * 1024;
    constexpr std::size_t kMaxLine = 4096; std::vector<char> buffer(kWindow + kMaxLine); std::size_t position = begin; std::size_t carry = 0;
    while (position < end) {
        const auto want = std::min({kWindow, end - position, buffer.size() - carry}); in.read(buffer.data() + carry, static_cast<std::streamsize>(want));
        const auto got = static_cast<std::size_t>(in.gcount()); if (got == 0) break; position += got; const auto total = carry + got; std::size_t start = 0;
        for (std::size_t i = 0; i < total; ++i) { if (buffer[i] != '\n') continue;
            parseEdgeLine(std::string_view(buffer.data() + start, i - start), n, visit);
            start = i + 1; }
        carry = total - start; std::memmove(buffer.data(), buffer.data() + start, carry);
        if (carry > kMaxLine) throw std::runtime_error("edge line too long"); }
    if (carry) parseEdgeLine(std::string_view(buffer.data(), carry), n, visit);
    if (position < end) throw std::runtime_error("failed reading graph"); }

Vertex checkedVertex(std::uint64_t value, std::uint32_t n, const char* name) {
    if (value >= n) throw std::runtime_error(std::string(name) + " outside node range");
    return static_cast<Vertex>(value); }

struct EdgeRecord {
    Vertex from; Vertex to;
    Weight weight; };

Graph readGraph(const fs::path& path, std::size_t workers) {
    const auto file_size = static_cast<std::size_t>(fs::file_size(path)); std::string head; std::size_t body_begin = 0; std::uint64_t n64 = 0, m64 = 0;
    { std::ifstream in(path, std::ios::binary);
        if (!in) throw std::runtime_error("cannot open graph: " + path.string()); head.resize(std::min<std::size_t>(file_size, 256)); in.read(head.data(), static_cast<std::streamsize>(head.size()));
        head.resize(static_cast<std::size_t>(in.gcount())); TokenStream header(head);
        if (!header.next(n64) || !header.next(m64))
            throw std::runtime_error("graph header must be n m");
        body_begin = header.position(); }
    if (n64 == 0 || n64 > std::numeric_limits<Vertex>::max())
        throw std::runtime_error("invalid graph n");
    if (m64 > std::numeric_limits<std::size_t>::max() / sizeof(EdgeRecord))
        throw std::runtime_error("invalid graph m");
    const auto n = static_cast<Vertex>(n64); const auto m = static_cast<std::size_t>(m64);

    std::string text; Graph graph; graph.vertices = n; std::vector<std::uint32_t> out_degree(n, 0), in_degree(n, 0);

    const auto chunk_count = std::min<std::size_t>(std::max<std::size_t>(1, workers) * 3, 48); std::vector<std::size_t> bounds(chunk_count + 1, body_begin);
    { std::ifstream in(path, std::ios::binary);
        if (!in) throw std::runtime_error("cannot open graph: " + path.string());
        for (std::size_t i = 1; i < chunk_count; ++i) {
            auto pos = body_begin + (file_size - body_begin) * i / chunk_count; pos = std::max(pos, bounds[i - 1]); in.seekg(static_cast<std::streamoff>(pos)); bool found = false;
            for (std::size_t step = 0; step < 4096 && pos < file_size; ++step) {
                const auto byte = in.get(); if (byte == std::char_traits<char>::eof()) break; ++pos;
                if (byte == '\n') { found = true; break; } }
            bounds[i] = found ? pos : file_size; }
        bounds[chunk_count] = file_size; }

    std::vector<std::vector<EdgeRecord>> edge_chunks(chunk_count);
    const auto count_chunks = [&] { std::atomic<bool> failed{false};
        parallelRanges(chunk_count, workers, [&](std::size_t begin, std::size_t end) {
            for (std::size_t i = begin; i < end && !failed.load(std::memory_order_relaxed); ++i) {
                try {
                    forEachEdgeChunk(path, bounds[i], bounds[i + 1], n,
                                     [&](Vertex u, Vertex v, Weight weight) {
                                        std::atomic_ref<std::uint32_t>(out_degree[u]).fetch_add(
                                            1, std::memory_order_relaxed);
                                        std::atomic_ref<std::uint32_t>(in_degree[v]).fetch_add(
                                            1, std::memory_order_relaxed);
                                        edge_chunks[i].push_back({u, v, weight});
                                    });
                } catch (const std::exception&) { failed.store(true, std::memory_order_relaxed); } }
        }); return !failed.load(std::memory_order_relaxed); };

    bool fast = count_chunks(); std::size_t seen = 0;
    if (fast) { for (Vertex v = 0; v < n; ++v) seen += out_degree[v]; fast = seen == m; }
    if (!fast) {
        std::vector<std::vector<EdgeRecord>>().swap(edge_chunks);
        text = readTextFile(path); std::fill(out_degree.begin(), out_degree.end(), 0); std::fill(in_degree.begin(), in_degree.end(), 0); TokenStream tokens(text); std::uint64_t skip = 0;
        if (!tokens.next(skip) || !tokens.next(skip))
            throw std::runtime_error("graph header must be n m");
        seen = 0; std::uint64_t u64 = 0, v64 = 0, w64 = 0;
        while (tokens.next(u64)) {
            if (!tokens.next(v64) || !tokens.next(w64))
                throw std::runtime_error("graph ends inside an edge");
            if (seen == m) throw std::runtime_error("graph has more edges than declared"); const auto u = checkedVertex(u64, n, "edge from"); const auto v = checkedVertex(v64, n, "edge to");
            if (w64 > std::numeric_limits<Weight>::max())
                throw std::runtime_error("edge weight outside uint32 range");
            ++out_degree[u]; ++in_degree[v];
            ++seen; }
        if (seen != m) throw std::runtime_error("graph has fewer edges than declared"); }

    graph.offset.assign(static_cast<std::size_t>(n) + 1, 0); graph.reverse_offset.assign(static_cast<std::size_t>(n) + 1, 0); std::size_t total = 0;
    for (Vertex v = 0; v < n; ++v) { graph.offset[v] = total; total += out_degree[v]; }
    graph.offset[n] = total; total = 0;
    for (Vertex v = 0; v < n; ++v) { graph.reverse_offset[v] = total; total += in_degree[v]; }
    graph.reverse_offset[n] = total; std::vector<std::uint32_t>().swap(out_degree); std::vector<std::uint32_t>().swap(in_degree); graph.arcs.resize(m); graph.reverse_arcs.resize(m);

    const auto scatter = [&](Vertex u, Vertex v, Weight w) {
        const auto forward = std::atomic_ref<std::size_t>(graph.offset[u]).fetch_add(
            1, std::memory_order_relaxed);
        graph.arcs[forward] = {v, w};
        const auto backward = std::atomic_ref<std::size_t>(graph.reverse_offset[v]).fetch_add(
            1, std::memory_order_relaxed);
        graph.reverse_arcs[backward] = {u, w}; };
    if (fast) {
        parallelRanges(chunk_count, workers, [&](std::size_t begin, std::size_t end) {
            for (std::size_t i = begin; i < end; ++i)
                for (const auto& edge : edge_chunks[i])
                    scatter(edge.from, edge.to, edge.weight);
        });
    } else { TokenStream tokens(text); std::uint64_t skip = 0;
        tokens.next(skip); tokens.next(skip); std::uint64_t u64 = 0, v64 = 0, w64 = 0;
        while (tokens.next(u64) && tokens.next(v64) && tokens.next(w64))
            scatter(static_cast<Vertex>(u64), static_cast<Vertex>(v64),
                    static_cast<Weight>(w64)); }
    std::vector<std::vector<EdgeRecord>>().swap(edge_chunks);
    for (std::size_t v = n; v > 0; --v) { graph.offset[v] = graph.offset[v - 1];
        graph.reverse_offset[v] = graph.reverse_offset[v - 1]; }
    graph.offset[0] = graph.reverse_offset[0] = 0; return graph; }

std::vector<Query> readQueries(const fs::path& path, std::uint32_t n) {
    const std::string text = readTextFile(path); TokenStream in(text); std::uint64_t count = 0; if (!in.next(count)) throw std::runtime_error("missing query count");
    if (count > std::numeric_limits<std::size_t>::max())
        throw std::runtime_error("query count too large");
    std::vector<Query> queries; queries.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t i = 0; i < count; ++i) { std::uint64_t from = 0, to = 0;
        if (!in.next(from) || !in.next(to))
            throw std::runtime_error("query file ends inside a query");
        queries.push_back({checkedVertex(from, n, "query source"),
                           checkedVertex(to, n, "query target")}); }
    return queries; }

std::optional<Distance> referenceDistance(const Graph& g, Vertex source, Vertex target) {
    if (source == target) return Distance{0}; using QueueItem = std::pair<Distance, Vertex>; std::priority_queue<QueueItem, std::vector<QueueItem>, std::greater<QueueItem>> queue;
    std::vector<Distance> distance(g.vertices, kInfinity); distance[source] = 0; queue.emplace(0, source);
    while (!queue.empty()) { const auto [known, u] = queue.top(); queue.pop();
        if (known != distance[u]) continue; if (u == target) return known;
        for (std::size_t p = g.offset[u]; p < g.offset[u + 1]; ++p) { const auto& arc = g.arcs[p];
            if (known > kInfinity - arc.weight) continue; const Distance candidate = known + arc.weight;
            if (candidate < distance[arc.to]) {
                distance[arc.to] = candidate; queue.emplace(candidate, arc.to); } } }
    return std::nullopt; }

class RadixQueue {
    using Item = std::pair<Distance, Vertex>; std::array<std::vector<Item>, 65> buckets_; Distance last_ = 0; std::size_t size_ = 0;

public:
    bool empty() const { return size_ == 0; }
    void clear() {
        for (auto& bucket : buckets_)
            bucket.clear();
        last_ = 0; size_ = 0; }
    void emplace(Distance distance, Vertex vertex) {
        if (distance < last_) throw std::logic_error("nonmonotone radix queue key");
        buckets_[std::bit_width(distance ^ last_)].emplace_back(distance, vertex); ++size_; }
    const Item& top() { if (empty()) throw std::logic_error("empty radix queue");
        if (buckets_[0].empty()) { std::size_t index = 1;
            while (buckets_[index].empty())
                ++index;
            last_ = std::min_element(buckets_[index].begin(), buckets_[index].end())->first;
            for (const auto& item : buckets_[index]) {
                buckets_[std::bit_width(item.first ^ last_)].push_back(item); }
            buckets_[index].clear(); }
        return buckets_[0].back(); }
    void pop() { top(); buckets_[0].pop_back();
        --size_; } };

constexpr std::uint32_t kUnreached = std::numeric_limits<std::uint32_t>::max();

class DistanceStore { std::vector<std::uint32_t> dense_;
    std::vector<std::uint64_t> slots_; std::vector<std::uint32_t> stamps_; std::size_t mask_ = 0; std::size_t used_ = 0; std::uint32_t stamp_ = 1;

    static std::size_t mix(std::uint32_t vertex) {
        return static_cast<std::size_t>((vertex * 0x9E3779B1u) >> 8); }

    void insertEntry(std::uint64_t entry) {
        const auto vertex = static_cast<std::uint32_t>((entry & 0xFFFFFFFFu) - 1); auto slot = mix(vertex) & mask_; while (stamps_[slot] == stamp_) slot = (slot + 1) & mask_;
        stamps_[slot] = stamp_; slots_[slot] = entry;
        ++used_; }

    void grow() { const auto capacity = (mask_ + 1) * 2; const auto old_mask = mask_;
        std::vector<std::uint64_t> old_slots(std::move(slots_)); std::vector<std::uint32_t> old_stamps(std::move(stamps_)); slots_.assign(capacity, 0);
        stamps_.assign(capacity, 0); mask_ = capacity - 1; used_ = 0;
        for (std::size_t i = 0; i <= old_mask; ++i)
            if (old_stamps[i] == stamp_) insertEntry(old_slots[i]); }

public:
    DistanceStore(std::uint32_t vertices, bool sparse) {
        if (!sparse) { dense_.assign(vertices, kUnreached); return; }
        constexpr std::size_t kInitial = 1 << 17; slots_.assign(kInitial, 0);
        stamps_.assign(kInitial, 0); mask_ = kInitial - 1; }

    void beginQuery(const std::vector<Vertex>& touched) {
        if (!dense_.empty()) {
            for (const auto vertex : touched) dense_[vertex] = kUnreached; return; }
        if (++stamp_ == 0) { std::fill(stamps_.begin(), stamps_.end(), 0); stamp_ = 1; }
        used_ = 0; }

    std::uint32_t get(std::uint32_t vertex) const {
        if (!dense_.empty()) return dense_[vertex]; auto slot = mix(vertex) & mask_;
        while (stamps_[slot] == stamp_) { const auto entry = slots_[slot];
            if (static_cast<std::uint32_t>(entry & 0xFFFFFFFFu) == vertex + 1)
                return static_cast<std::uint32_t>(entry >> 32);
            slot = (slot + 1) & mask_; }
        return kUnreached; }

    void set(std::uint32_t vertex, std::uint32_t distance) {
        if (!dense_.empty()) { dense_[vertex] = distance; return; }
        const auto entry = (static_cast<std::uint64_t>(distance) << 32) | (vertex + 1); auto slot = mix(vertex) & mask_;
        while (stamps_[slot] == stamp_) {
            if (static_cast<std::uint32_t>(slots_[slot] & 0xFFFFFFFFu) == vertex + 1) {
                slots_[slot] = entry; return; }
            slot = (slot + 1) & mask_; }
        stamps_[slot] = stamp_; slots_[slot] = entry;
        if (++used_ * 4 >= (mask_ + 1) * 3) grow(); } };

class CandidateSearch { const Graph& g; DistanceStore from_source, to_target;
    std::vector<Vertex> forward_touched, backward_touched; RadixQueue forward, backward; std::vector<Vertex> component; bool component_attempted = false; bool needs_wide_ = false;

    void buildComponents() { component_attempted = true; component.assign(g.vertices, kUnreached);
        std::vector<Vertex> stack; Vertex count = 0;
        for (Vertex root = 0; root < g.vertices; ++root) {
            if (component[root] != kUnreached) continue; component[root] = count; stack.push_back(root);
            while (!stack.empty()) { const auto u = stack.back(); stack.pop_back();
                for (auto p = g.offset[u]; p < g.offset[u + 1]; ++p) {
                    const auto v = g.arcs[p].to; if (component[v] != kUnreached) continue;
                    component[v] = count; stack.push_back(v); }
                for (auto p = g.reverse_offset[u]; p < g.reverse_offset[u + 1]; ++p) {
                    const auto v = g.reverse_arcs[p].to; if (component[v] != kUnreached) continue;
                    component[v] = count; stack.push_back(v); } }
            ++count; }
        if (count == 1) std::vector<Vertex>().swap(component); }

public:
    CandidateSearch(const Graph& graph, std::size_t queries)
        : g(graph),
          from_source(g.vertices, sparseFor(g.vertices)),
          to_target(g.vertices, sparseFor(g.vertices)) {
        (void)queries; }

    static bool sparseFor(std::uint32_t vertices) { (void)vertices; return false; }

    bool needs_wide() const { return needs_wide_; }


    static std::uint64_t sum(std::uint64_t left, std::uint64_t right) {
        if (left >= kUnreached || right >= kUnreached) return kUnreached;
        const auto total = left + right; return total >= kUnreached ? kUnreached : total; }

    std::optional<Distance> distance(Vertex source, Vertex target) {
        needs_wide_ = false; from_source.beginQuery(forward_touched); to_target.beginQuery(backward_touched); forward_touched.clear(); backward_touched.clear(); forward.clear(); backward.clear();
        if (source == target) return Distance{0}; if (!component.empty() && component[source] != component[target]) return std::nullopt;

        from_source.set(source, 0); to_target.set(target, 0); forward_touched.push_back(source); backward_touched.push_back(target);
        forward.emplace(0, source); backward.emplace(0, target); std::uint64_t best = kUnreached;

        const auto discard_stale = [](RadixQueue& queue, const DistanceStore& distances) {
            while (!queue.empty() && queue.top().first != distances.get(queue.top().second)) {
                queue.pop(); } };
        const auto relax = [&](Vertex to, std::uint64_t candidate, DistanceStore& distances,
                               std::vector<Vertex>& touched, RadixQueue& queue) {
            if (candidate >= kUnreached) { needs_wide_ = true; return; }
            const auto known = distances.get(to); if (candidate >= known) return; if (known == kUnreached) touched.push_back(to);
            distances.set(to, static_cast<std::uint32_t>(candidate)); queue.emplace(candidate, to); };

        while (!forward.empty() && !backward.empty() && !needs_wide_) {
            discard_stale(forward, from_source); discard_stale(backward, to_target); if (forward.empty() || backward.empty()) break;
            const auto lower_bound = sum(forward.top().first, backward.top().first);
            if (lower_bound >= best) {
                if (lower_bound >= kUnreached) needs_wide_ = true; break; }

            const auto forward_vertex = forward.top().second; const auto backward_vertex = backward.top().second;
            const auto forward_work = g.offset[forward_vertex + 1] - g.offset[forward_vertex];
            const auto backward_work =
                g.reverse_offset[backward_vertex + 1] - g.reverse_offset[backward_vertex];
            if (forward_work <= backward_work) {
                const auto [known, vertex] = forward.top(); forward.pop(); best = std::min(best, sum(known, to_target.get(vertex)));
                for (std::size_t p = g.offset[vertex]; p < g.offset[vertex + 1]; ++p) {
                    const auto& arc = g.arcs[p]; const auto candidate = sum(known, arc.weight); relax(arc.to, candidate, from_source, forward_touched, forward);
                    best = std::min(best, sum(candidate, to_target.get(arc.to))); }
            } else { const auto [known, vertex] = backward.top(); backward.pop();
                best = std::min(best, sum(from_source.get(vertex), known)); for (std::size_t p = g.reverse_offset[vertex]; p < g.reverse_offset[vertex + 1];
                     ++p) {
                    const auto& arc = g.reverse_arcs[p]; const auto candidate = sum(known, arc.weight); relax(arc.to, candidate, to_target, backward_touched, backward);
                    best = std::min(best, sum(from_source.get(arc.to), candidate)); } } }
        if (needs_wide_) return std::nullopt;
        if (best >= kUnreached) {
            if (!component_attempted &&
                forward_touched.size() + backward_touched.size() >=
                    std::max<std::size_t>(1, g.vertices / 8))
                buildComponents();
            return std::nullopt; }
        return best; } };

struct Options { fs::path graph, queries, output; std::string algorithm = "candidate";
    std::size_t workers = practice::defaultWorkers; bool profile = false; };

std::size_t parseWorkers(std::string_view text) {
    if (text.empty()) throw std::runtime_error("threads must be a positive integer"); std::size_t value = 0; const auto* start = text.data();
    const auto* end = start + text.size(); auto [stop, error] = std::from_chars(start, end, value);
    if (error != std::errc{} || stop != end || value == 0 || value > practice::maxWorkers)
        throw std::runtime_error("threads must be an integer in 1..12 (EX44 budget)");
    return value; }

Options parseArgs(int argc, char** argv) { Options options;
    for (int i = 1; i < argc; ++i) { const std::string arg = argv[i];
        if (arg == "--profile") {
            if (options.profile) throw std::runtime_error("duplicate option: --profile");
            options.profile = true; continue; }
        if (i + 1 >= argc) throw std::runtime_error("missing value after " + arg); const std::string value = argv[++i];
        if (arg == "--graph")
            options.graph = value;
        else if (arg == "--queries")
            options.queries = value;
        else if (arg == "--output")
            options.output = value;
        else if (arg == "--algorithm")
            options.algorithm = value;
        else if (arg == "--threads")
            options.workers = parseWorkers(value);
        else
            throw std::runtime_error("unknown option: " + arg); }
    if (options.graph.empty() || options.queries.empty() || options.output.empty())
        throw std::runtime_error("usage: speeddemon_solver --graph FILE --queries FILE --output FILE "
                                 "[--algorithm oracle|candidate] [--threads N]");
    if (options.algorithm != "oracle" && options.algorithm != "candidate")
        throw std::runtime_error("algorithm must be candidate or oracle");
    return options; }

int runSolver(int argc, char** argv) {
    const auto opt = parseArgs(argc, argv); const auto start = std::chrono::steady_clock::now();
    auto graph = readGraph(opt.graph, opt.workers); auto queries = readQueries(opt.queries, graph.vertices); std::vector<std::optional<Distance>> answers;
    if (opt.algorithm == "oracle") { answers.resize(queries.size());
        practice::parallelRanges(queries.size(), opt.workers, [&](std::size_t begin, std::size_t end) {
            for (std::size_t i = begin; i < end; ++i) { const auto& query = queries[i];
                answers[i] = referenceDistance(graph, query.from, query.to); }
        });
    } else { std::vector<Query> unique_queries; std::vector<std::size_t> query_indices;
        unique_queries.reserve(queries.size()); query_indices.reserve(queries.size()); std::unordered_map<std::uint64_t, std::size_t> indices; indices.reserve(queries.size());
        for (const auto& query : queries) {
            const auto key = (static_cast<std::uint64_t>(query.from) << 32) | query.to; const auto [found, inserted] = indices.try_emplace(key, unique_queries.size());
            if (inserted) unique_queries.push_back(query); query_indices.push_back(found->second); }
        constexpr std::size_t kScalingLimit = 12;
        const auto memory_cap = static_cast<std::size_t>(std::max(
            1.0, 2.0e9 / static_cast<double>(2 * sizeof(std::uint32_t) * graph.vertices + 1)));
        const auto query_threads =
            std::min({opt.workers, kScalingLimit, std::max<std::size_t>(1, memory_cap)});        std::vector<std::optional<Distance>> unique_answers(unique_queries.size());
        const auto active_workers = std::min(query_threads, unique_queries.size());
        std::atomic<std::size_t> next_query{0};
        practice::parallelRanges(active_workers, active_workers, [&](std::size_t, std::size_t) {
            CandidateSearch search(graph, unique_queries.size()); for (auto i = next_query.fetch_add(1, std::memory_order_relaxed);
                 i < unique_queries.size();
                 i = next_query.fetch_add(1, std::memory_order_relaxed)) {
                const auto& query = unique_queries[i]; unique_answers[i] = search.distance(query.from, query.to);
                if (search.needs_wide())
                    unique_answers[i] = referenceDistance(graph, query.from, query.to); }
        }); answers.reserve(queries.size());
        for (const auto index : query_indices)
            answers.push_back(unique_answers[index]); }
    std::ofstream out(opt.output, std::ios::binary); if (!out) throw std::runtime_error("cannot open output: " + opt.output.string());
    for (const auto result : answers) {
        if (result)
            out << *result << '\n';
        else
            out << "UNREACHABLE\n"; }
    if (!out) throw std::runtime_error("failed writing output");
    if (opt.profile) {
        practice::writeRunProfile(std::cerr, "speeddemon_solver_profile",
                                   {"shortest_path", opt.algorithm, opt.workers,
                                    practice::elapsedSeconds(start)});
        if (!std::cerr) throw std::runtime_error("failed writing solver profile"); }
    return 0; } }

int main(int argc, char** argv) {
    try { return practice::runSolver(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "speeddemon_solver: " << e.what() << '\n'; return 1; } }
