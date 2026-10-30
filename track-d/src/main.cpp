#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <sys/stat.h>

using u32 = std::uint32_t;
using u64 = std::uint64_t;
constexpr u64 infinity = UINT64_MAX;
struct __attribute__((packed)) Edge { u32 to; std::uint16_t weight; };
static_assert(sizeof(Edge) == 6);
struct Query { u32 from, to; };

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

u32 number(const char*& p) {
    u32 value = 0;
    while (*p >= '0' && *p <= '9') value = value * 10 + static_cast<unsigned>(*p++ - '0');
    return value;
}

template<class Visit>
void edges_in_range(std::size_t begin, std::size_t end, Visit visit) {
    std::vector<char> buffer(256 * 1024);
    auto close_file = [](FILE* file) { if (file) std::fclose(file); };
    std::unique_ptr<FILE, decltype(close_file)> file(std::fopen("/data/graph.txt", "rb"), close_file);
    if (!file) throw std::runtime_error("cannot open graph.txt");
    std::size_t position = begin;
    if (begin) {
        if (fseeko(file.get(), static_cast<off_t>(begin - 1), SEEK_SET))
            throw std::runtime_error("cannot seek graph.txt");
        if (std::fgetc(file.get()) != '\n') {
            int c;
            do {
                c = std::fgetc(file.get());
                if (c == EOF) return;
                ++position;
            } while (c != '\n');
        }
    }
    std::size_t carry = 0;
    while (position < end) {
        const auto got = std::fread(buffer.data() + carry, 1, buffer.size() - carry, file.get());
        if (!got) throw std::runtime_error("truncated graph.txt");
        const char* p = buffer.data();
        const char* stop = p + carry + got;
        const char* complete = stop;
        while (complete != p && complete[-1] != '\n') --complete;
        while (position < end && p < complete) {
            const char* row = p;
            const u32 from = number(p);
            if (*p++ != ' ') throw std::runtime_error("invalid edge source");
            const u32 to = number(p);
            if (*p++ != ' ') throw std::runtime_error("invalid edge target");
            const u32 weight = number(p);
            if (*p++ != '\n') throw std::runtime_error("invalid edge weight");
            visit(from, to, weight);
            position += static_cast<std::size_t>(p - row);
        }
        carry = static_cast<std::size_t>(stop - p);
        if (carry == buffer.size()) throw std::runtime_error("edge line exceeds buffer");
        std::memmove(buffer.data(), p, carry);
    }
    if (std::ferror(file.get())) throw std::runtime_error("cannot read graph.txt");
}

class RadixQueue {
    struct __attribute__((packed)) Item { u64 first; u32 second; };
    static_assert(sizeof(Item) == 12);
    std::array<std::vector<Item>, 65> buckets_;
    u64 last_ = 0;
    std::size_t size_ = 0;
public:
    bool empty() const { return size_ == 0; }
    std::size_t size() const { return size_; }
    void clear() {
        for (auto& bucket : buckets_) bucket.clear();
        last_ = 0;
        size_ = 0;
    }
    void push(u64 distance, u32 node) {
        buckets_[std::bit_width(distance ^ last_)].emplace_back(distance, node);
        ++size_;
    }
    Item top() {
        if (buckets_[0].empty()) {
            unsigned b = 1;
            while (buckets_[b].empty()) ++b;
            last_ = std::min_element(buckets_[b].begin(), buckets_[b].end(),
                [](const Item& a, const Item& b) { return a.first < b.first; })->first;
            for (const Item& item : buckets_[b])
                buckets_[std::bit_width(item.first ^ last_)].push_back(item);
            buckets_[b].clear();
        }
        return buckets_[0].back();
    }
    void pop() { buckets_[0].pop_back(); --size_; }
};

class Distances {
    struct Slot { u64 distance = 0; u32 node = 0, stamp = 0; };
    std::vector<Slot> slots_ = std::vector<Slot>(1024);
    u32 stamp_ = 1;
    std::size_t count_ = 0;
    static std::size_t hash(u32 node) {
        const u64 product = u64{node} * 0x9e3779b97f4a7c15ULL;
        return product ^ (product >> 32);
    }
    void grow() {
        auto old = std::move(slots_);
        slots_.resize(old.size() * 2);
        count_ = 0;
        for (const auto& slot : old) if (slot.stamp == stamp_) relax(slot.node, slot.distance);
    }
public:
    void clear() {
        count_ = 0;
        if (++stamp_ == 0) {
            for (auto& slot : slots_) slot.stamp = 0;
            stamp_ = 1;
        }
    }
    u64 get(u32 node) const {
        auto s = hash(node) & (slots_.size() - 1);
        while (slots_[s].stamp == stamp_) {
            if (slots_[s].node == node) return slots_[s].distance;
            s = (s + 1) & (slots_.size() - 1);
        }
        return infinity;
    }
    bool relax(u32 node, u64 distance) {
        auto s = hash(node) & (slots_.size() - 1);
        while (slots_[s].stamp == stamp_) {
            if (slots_[s].node == node) {
                if (distance >= slots_[s].distance) return false;
                slots_[s].distance = distance;
                return true;
            }
            s = (s + 1) & (slots_.size() - 1);
        }
        slots_[s] = {distance, node, stamp_};
        if (++count_ * 2 > slots_.size()) grow();
        return true;
    }
};

struct Graph {
    std::array<std::vector<u32>, 2> offsets;
    std::array<std::unique_ptr<Edge[]>, 2> edges;
    std::array<std::unordered_map<u32, u32>, 2> wide_weights;
};

class Search {
    const Graph& graph_;
    std::array<Distances, 2> distances_;
    std::array<RadixQueue, 2> queues_;
public:
    explicit Search(const Graph& graph) : graph_(graph) {}
    std::int64_t run(u32 source, u32 target) {
        for (auto& distances : distances_) distances.clear();
        if (source == target) return 0;
        if (graph_.offsets[0][source] == graph_.offsets[0][source + 1] ||
            graph_.offsets[1][target] == graph_.offsets[1][target + 1]) return -1;
        for (unsigned d = 0; d < 2; ++d) {
            queues_[d].clear();
            const u32 node = d ? target : source;
            distances_[d].relax(node, 0);
            queues_[d].push(0, node);
        }
        u64 best = infinity;
        for (;;) {
            for (unsigned d = 0; d < 2; ++d)
                while (!queues_[d].empty() && queues_[d].top().first != distances_[d].get(queues_[d].top().second))
                    queues_[d].pop();
            if (queues_[0].empty() || queues_[1].empty()) break;
            if (best != infinity && queues_[0].top().first + queues_[1].top().first >= best) break;
            // Balance search work, not distances: injected edges may weigh almost 2^32.
            const unsigned d = queues_[0].size() <= queues_[1].size() ? 0 : 1;
            const auto [distance, node] = queues_[d].top();
            queues_[d].pop();
            for (u32 i = graph_.offsets[d][node]; i < graph_.offsets[d][node + 1]; ++i) {
                const Edge& edge = graph_.edges[d][i];
                const u32 weight = edge.weight == UINT16_MAX ? graph_.wide_weights[d].at(i) : edge.weight;
                const u64 next = distance + weight;
                if (next >= best) continue;
                const u64 other = distances_[1 - d].get(edge.to);
                if (other != infinity) best = std::min(best, next + other);
                if (next < best && distances_[d].relax(edge.to, next)) queues_[d].push(next, edge.to);
            }
        }
        return best == infinity ? -1 : static_cast<std::int64_t>(best);
    }
};

int solve() {
    std::ifstream input("/data/graph.txt");
    u32 n, m;
    if (!(input >> n >> m) || !n) throw std::runtime_error("invalid graph header");
    input.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    const auto header = static_cast<std::size_t>(input.tellg());
    struct stat status{};
    if (stat("/data/graph.txt", &status)) throw std::runtime_error("cannot stat graph.txt");
    const auto size = static_cast<std::size_t>(status.st_size);
    const unsigned workers = m < 10000 ? 1 : 12;
    Graph graph;
    for (unsigned d = 0; d < 2; ++d) graph.offsets[d].resize(static_cast<std::size_t>(n) + 1);
    const auto read = [&](auto visit) {
        parallel(workers, [&](unsigned w) {
            edges_in_range(header + (size - header) * w / workers,
                           header + (size - header) * (w + 1) / workers, visit);
        });
    };
    read([&](u32 from, u32 to, u32) {
        if (from >= n || to >= n) throw std::runtime_error("edge outside graph");
        std::atomic_ref<u32>(graph.offsets[0][from + 1]).fetch_add(1, std::memory_order_relaxed);
        std::atomic_ref<u32>(graph.offsets[1][to + 1]).fetch_add(1, std::memory_order_relaxed);
    });
    for (unsigned d = 0; d < 2; ++d) {
        for (std::size_t i = 1; i < graph.offsets[d].size(); ++i)
            graph.offsets[d][i] += graph.offsets[d][i - 1];
        if (graph.offsets[d].back() != m) throw std::runtime_error("incorrect edge count");
        graph.edges[d] = std::make_unique_for_overwrite<Edge[]>(m);
    }
    {
        auto cursors = graph.offsets;
        std::mutex wide_mutex;
        read([&](u32 from, u32 to, u32 weight) {
            const auto a = std::atomic_ref<u32>(cursors[0][from]).fetch_add(1, std::memory_order_relaxed);
            const auto b = std::atomic_ref<u32>(cursors[1][to]).fetch_add(1, std::memory_order_relaxed);
            const auto narrow = static_cast<std::uint16_t>(std::min<u32>(weight, UINT16_MAX));
            graph.edges[0][a] = {to, narrow};
            graph.edges[1][b] = {from, narrow};
            if (weight >= UINT16_MAX) {
                std::lock_guard lock(wide_mutex);
                graph.wide_weights[0].emplace(a, weight);
                graph.wide_weights[1].emplace(b, weight);
            }
        });
    }
    std::ifstream query_file("/data/queries.txt");
    if (!query_file) throw std::runtime_error("cannot open queries.txt");
    std::vector<Query> queries;
    u32 source, target;
    while (query_file >> source) {
        if (!(query_file >> target) || source >= n || target >= n)
            throw std::runtime_error("invalid query");
        queries.push_back({source, target});
    }
    if (!query_file.eof()) throw std::runtime_error("cannot read queries.txt");
    std::vector<std::int64_t> answers(queries.size());
    std::atomic<std::size_t> next{0};
    parallel(workers, [&](unsigned) {
        Search search(graph);
        for (;;) {
            const auto q = next.fetch_add(1, std::memory_order_relaxed);
            if (q >= queries.size()) break;
            answers[q] = search.run(queries[q].from, queries[q].to);
        }
    });
    std::ofstream output("/out/distances.txt", std::ios::binary);
    if (!output) throw std::runtime_error("cannot open distances.txt");
    for (auto answer : answers) output << answer << '\n';
    output.close();
    if (!output) throw std::runtime_error("cannot write distances.txt");
    return 0;
}

int main() {
    try { return solve(); }
    catch (const std::exception& error) { std::fprintf(stderr, "track-d: %s\n", error.what()); return 1; }
}
