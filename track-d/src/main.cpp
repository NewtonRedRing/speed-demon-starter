#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <fstream>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class FastInput {
    static constexpr std::size_t capacity = 1 << 20;
    FILE* file_;
    char buffer_[capacity];
    std::size_t position_ = 0;
    std::size_t size_ = 0;

    int next_char() {
        if (position_ == size_) {
            size_ = std::fread(buffer_, 1, capacity, file_);
            position_ = 0;
            if (size_ == 0) return EOF;
        }
        return static_cast<unsigned char>(buffer_[position_++]);
    }

public:
    explicit FastInput(const char* path) : file_(std::fopen(path, "rb")) {}
    ~FastInput() { if (file_) std::fclose(file_); }
    bool good() const { return file_ != nullptr; }

    bool read_number(std::uint64_t& value) {
        int c;
        do {
            c = next_char();
            if (c == EOF) return false;
        } while (c < '0' || c > '9');
        value = 0;
        do {
            value = value * 10 + static_cast<unsigned>(c - '0');
            c = next_char();
        } while (c >= '0' && c <= '9');
        return true;
    }
};

struct InputEdge {
    std::uint32_t from;
    std::uint32_t to;
    std::uint32_t weight;
};

struct Edge {
    std::uint32_t to;
    std::uint32_t weight;
};

struct Query {
    std::uint32_t source;
    std::uint32_t target;
};

// Four-way heap: fewer levels than a binary heap and no temporary
// push_heap/pop_heap range adjustments for each graph relaxation.
class MinHeap {
    using Item = std::pair<std::uint64_t, std::uint32_t>;
    std::vector<Item> entries_;

public:
    void clear() { entries_.clear(); }
    bool empty() const { return entries_.empty(); }

    void push(Item item) {
        std::size_t hole = entries_.size();
        entries_.emplace_back();
        while (hole != 0) {
            const std::size_t parent = (hole - 1) / 4;
            if (!(item < entries_[parent])) break;
            entries_[hole] = entries_[parent];
            hole = parent;
        }
        entries_[hole] = item;
    }

    Item pop() {
        const Item top = entries_.front();
        const Item tail = entries_.back();
        entries_.pop_back();
        if (entries_.empty()) return top;
        std::size_t hole = 0;
        for (;;) {
            const std::size_t first = hole * 4 + 1;
            if (first >= entries_.size()) break;
            std::size_t best = first;
            const std::size_t end = std::min(entries_.size(), first + 4);
            for (std::size_t child = first + 1; child < end; ++child) {
                if (entries_[child] < entries_[best]) best = child;
            }
            if (!(entries_[best] < tail)) break;
            entries_[hole] = entries_[best];
            hole = best;
        }
        entries_[hole] = tail;
        return top;
    }
};

int main() {
    FastInput graph("/data/graph.txt");
    if (!graph.good()) return 1;
    std::uint64_t raw_n, raw_m;
    if (!graph.read_number(raw_n) || !graph.read_number(raw_m)) return 1;
    const std::uint32_t n = static_cast<std::uint32_t>(raw_n);
    const std::uint32_t m = static_cast<std::uint32_t>(raw_m);
    std::vector<std::uint32_t> offsets(static_cast<std::size_t>(n) + 1, 0);
    std::vector<InputEdge> input_edges;
    input_edges.reserve(m);
    for (std::uint32_t i = 0; i < m; ++i) {
        std::uint64_t u, v, w;
        if (!graph.read_number(u) || !graph.read_number(v) || !graph.read_number(w)) return 1;
        input_edges.push_back({static_cast<std::uint32_t>(u), static_cast<std::uint32_t>(v),
                               static_cast<std::uint32_t>(w)});
        ++offsets[static_cast<std::size_t>(u) + 1];
    }
    for (std::size_t i = 1; i < offsets.size(); ++i) offsets[i] += offsets[i - 1];
    std::vector<std::uint32_t> cursor = offsets;
    std::vector<Edge> edges(m);
    for (const InputEdge& edge : input_edges)
        edges[cursor[edge.from]++] = {edge.to, edge.weight};
    std::vector<InputEdge>().swap(input_edges);
    std::vector<std::uint32_t>().swap(cursor);

    FastInput query_file("/data/queries.txt");
    if (!query_file.good()) return 1;
    std::vector<Query> queries;
    std::uint64_t s, t;
    while (query_file.read_number(s)) {
        if (!query_file.read_number(t)) return 1;
        queries.push_back({static_cast<std::uint32_t>(s), static_cast<std::uint32_t>(t)});
    }
    if (queries.empty()) return 1;

    std::unordered_map<std::uint32_t, std::vector<std::size_t>> source_queries;
    source_queries.reserve(queries.size());
    for (std::size_t i = 0; i < queries.size(); ++i)
        source_queries[queries[i].source].push_back(i);

    std::vector<std::int64_t> answers(queries.size(), -1);
    std::vector<std::uint64_t> distance(n);
    std::vector<std::uint32_t> seen(n, 0);
    MinHeap heap;
    std::uint32_t generation = 0;

    for (const auto& [source, indexes] : source_queries) {
        std::unordered_map<std::uint32_t, std::vector<std::size_t>> targets;
        targets.reserve(indexes.size());
        for (std::size_t index : indexes) {
            const std::uint32_t target = queries[index].target;
            if (target == source) answers[index] = 0;
            else targets[target].push_back(index);
        }
        if (targets.empty()) continue;

        ++generation;
        std::size_t remaining = targets.size();
        heap.clear();
        seen[source] = generation;
        distance[source] = 0;
        heap.push({0, source});

        while (!heap.empty() && remaining != 0) {
            const auto [current, u] = heap.pop();
            if (seen[u] != generation || current != distance[u]) continue;

            auto target_it = targets.find(u);
            if (target_it != targets.end()) {
                for (std::size_t index : target_it->second)
                    answers[index] = static_cast<std::int64_t>(current);
                targets.erase(target_it);
                if (--remaining == 0) break;
            }

            for (std::uint32_t i = offsets[u]; i < offsets[static_cast<std::size_t>(u) + 1]; ++i) {
                const Edge& edge = edges[i];
                const std::uint64_t next = current + edge.weight;
                if (seen[edge.to] != generation || next < distance[edge.to]) {
                    seen[edge.to] = generation;
                    distance[edge.to] = next;
                    heap.push({next, edge.to});
                }
            }
        }
    }

    std::ofstream output("/out/distances.txt", std::ios::binary);
    if (!output) return 1;
    output.rdbuf()->pubsetbuf(nullptr, 256 * 1024);
    for (std::int64_t answer : answers) output << answer << '\n';
    return output ? 0 : 1;
}
