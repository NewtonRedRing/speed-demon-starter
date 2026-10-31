#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>
#include <sys/mman.h>
#include <sys/stat.h>

using u32 = std::uint32_t;
using u64 = std::uint64_t;
constexpr unsigned hours = 72;
constexpr unsigned endpoint_capacity = 1842;

u64 endpoint_hash(std::string_view name) {
    u64 hash = name.size();
    while (name.size() >= sizeof(u64)) {
        u64 word;
        std::memcpy(&word, name.data(), sizeof(word));
        hash = (hash ^ word) * 0x9e3779b97f4a7c15ULL;
        hash ^= hash >> 29;
        name.remove_prefix(sizeof(word));
    }
    for (unsigned char c : name) hash = (hash ^ c) * 0x100000001b3ULL;
    return hash ^ (hash >> 32);
}

struct Group {
    u32 count = 0;
    u32 errors = 0;
    u32 offset = 0;
};
struct Record {
    u32 latency;
    u32 key;
};
struct Local {
    std::array<u64, 4096> ids{};
    std::vector<std::string> endpoints;
    std::array<std::vector<Record>, hours> records;
    std::vector<u32> remap;
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

void parse(Local& local, const char* data, std::size_t begin, std::size_t end) {
    if (begin && data[begin - 1] != '\n')
        while (data[begin++] != '\n') {}
    if (end && data[end - 1] != '\n')
        while (data[end++] != '\n') {}
    local.endpoints.reserve(endpoint_capacity);
    const auto row = [&](const char* p, const char* limit) {
        const unsigned month = (p[5] - '0') * 10 + p[6] - '0';
        const unsigned day = (p[8] - '0') * 10 + p[9] - '0';
        const unsigned hour = (p[11] - '0') * 10 + p[12] - '0';
        const unsigned h = (month == 10 ? day - 30 : day + 1) * 24 + hour - 18;
        p += 28;
        while (*p++ != ' ') {}
        const char* endpoint = p;
        p = static_cast<const char*>(std::memchr(p, ' ', limit - p));
        if (!p) throw std::runtime_error("missing endpoint delimiter");
        const std::string_view name(endpoint, p - endpoint);
        const u64 hash = endpoint_hash(name);
        auto index = hash & (local.ids.size() - 1);
        while (local.ids[index]) {
            const u32 candidate = static_cast<u32>(local.ids[index]) - 1;
            if ((local.ids[index] >> 32) == (hash >> 32) && local.endpoints[candidate] == name) break;
            index = (index + 1) & (local.ids.size() - 1);
        }
        u32 id;
        if (!local.ids[index]) {
            id = static_cast<u32>(local.endpoints.size());
            if (id == endpoint_capacity) throw std::runtime_error("endpoint catalogue exceeds capacity");
            local.ids[index] = (hash & 0xffffffff00000000ULL) | (u64{id} + 1);
            local.endpoints.emplace_back(name);
        } else id = static_cast<u32>(local.ids[index]) - 1;
        const bool error = p[1] == '5';
        p += 5;
        u32 latency = 0;
        while (*p >= '0' && *p <= '9') latency = latency * 10 + static_cast<unsigned>(*p++ - '0');
        auto& records = local.records[h];
        if (records.empty()) records.reserve((end - begin) / (64 * hours) + 1);
        records.push_back({latency, id | (u32(error) << 11)});
        return p + 1;
    };
    const char* p = data + begin;
    auto released = (begin + 4095) & ~std::size_t(4095);
    while (p < data + end) {
        p = row(p, data + end);
        const auto consumed = static_cast<std::size_t>(p - data);
        if (consumed > released && consumed - released >= 1024 * 1024) {
            const auto stop = consumed & ~std::size_t(4095);
            if (madvise(const_cast<char*>(data) + released, stop - released, MADV_DONTNEED))
                throw std::runtime_error("cannot release consumed request pages");
            released = stop;
        }
    }
}

void append(char*& output, u64 value, char separator) {
    auto result = std::to_chars(output, output + 20, value);
    output = result.ptr;
    *output++ = separator;
}

int solve() {
    struct stat status{};
    if (stat("/data/requests.log", &status)) throw std::runtime_error("cannot stat requests.log");
    const auto size = static_cast<std::size_t>(status.st_size);
    auto close_input = [](FILE* file) { if (file) std::fclose(file); };
    std::unique_ptr<FILE, decltype(close_input)> input(std::fopen("/data/requests.log", "rb"), close_input);
    if (!input) throw std::runtime_error("cannot open requests.log");
    auto unmap = [size](void* p) { if (p) munmap(p, size); };
    std::unique_ptr<void, decltype(unmap)> mapping(nullptr, unmap);
    if (size) {
        void* p = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fileno(input.get()), 0);
        if (p == MAP_FAILED) throw std::runtime_error("cannot map requests.log");
        mapping.reset(p);
    }
    const char* data = size ? static_cast<const char*>(mapping.get()) : "";
    const unsigned workers = size < 1024 * 1024 ? 1 : 12;
    std::vector<Local> locals(workers);
    parallel(workers, [&](unsigned w) { parse(locals[w], data, size * w / workers, size * (w + 1) / workers); });
    std::unordered_map<std::string, u32> ids;
    std::vector<std::string> endpoints;
    ids.reserve(1842);
    endpoints.reserve(1842);
    for (Local& local : locals) {
        local.remap.resize(local.endpoints.size());
        for (u32 i = 0; i < local.endpoints.size(); ++i) {
            const auto& endpoint = local.endpoints[i];
            const auto [it, inserted] = ids.try_emplace(endpoint, static_cast<u32>(endpoints.size()));
            if (inserted) endpoints.push_back(endpoint);
            local.remap[i] = it->second;
        }
    }
    std::sort(endpoints.begin(), endpoints.end());
    std::vector<u32> ranks(endpoints.size());
    for (u32 i = 0; i < endpoints.size(); ++i) ranks[ids.at(endpoints[i])] = i;
    const auto endpoint_count = static_cast<u32>(endpoints.size());
    std::vector<Group> groups(endpoint_count * hours);
    for (Local& local : locals)
        for (u32& id : local.remap) id = ranks[id];
    std::array<u32, hours> hour_offsets{};
    u32 total = 0;
    for (unsigned h = 0; h < hours; ++h) {
        hour_offsets[h] = total;
        for (const Local& local : locals) total += static_cast<u32>(local.records[h].size());
    }
    auto values = std::make_unique_for_overwrite<u32[]>(total);
    struct Output {
        std::unique_ptr<char[]> data;
        std::size_t size = 0;
    };
    std::array<Output, hours> outputs;
    std::atomic<unsigned> next{0};
    parallel(workers, [&](unsigned) {
      for (;;) {
        const unsigned h = next.fetch_add(1, std::memory_order_relaxed);
        if (h >= hours) break;
        Group* hour_groups = endpoint_count ? groups.data() + h * endpoint_count : nullptr;
        // Count in an hour-local cache-resident table instead of issuing random
        // counter writes while parsing the input file.
        for (const Local& local : locals)
            for (const Record& record : local.records[h]) {
                Group& target = hour_groups[local.remap[record.key & 2047u]];
                ++target.count;
                target.errors += record.key >> 11;
            }
        u32 offset = hour_offsets[h];
        for (u32 id = 0; id < endpoint_count; ++id) {
            hour_groups[id].offset = offset;
            offset += hour_groups[id].count;
        }
        for (Local& local : locals) {
            for (const Record& record : local.records[h])
                values[hour_groups[local.remap[record.key & 2047u]].offset++] = record.latency;
            std::vector<Record>().swap(local.records[h]);
        }
        for (u32 id = 0; id < endpoint_count; ++id) hour_groups[id].offset -= hour_groups[id].count;
        const unsigned day = (h + 18) / 24;
        char hour[32];
        std::snprintf(hour, sizeof(hour), "2026-%02u-%02uT%02u:00:00Z",
                      day < 2 ? 10 : 11, day < 2 ? day + 30 : day - 1, (h + 18) % 24);
        auto& output = outputs[h];
        // A maximum-length row uses 213 bytes; leave room for to_chars' bound.
        output.data = std::make_unique_for_overwrite<char[]>(endpoints.size() * 224);
        char* cursor = output.data.get();
        for (u32 id = 0; id < endpoint_count; ++id) {
            Group& group = groups[h * endpoint_count + id];
            u32* begin = values.get() + group.offset;
            const auto count = group.count;
            if (!count) continue;
            std::array<u32, 3> percentiles;
            if (count <= 32) std::sort(begin, begin + count);
            auto start = begin;
            for (unsigned j = 0; j < 3; ++j) {
                constexpr unsigned ranks[]{50, 95, 99};
                auto rank = begin + (ranks[j] * count + 99) / 100 - 1;
                if (count > 32) std::nth_element(start, rank, begin + count);
                percentiles[j] = *rank;
                start = rank;
            }
            std::memcpy(cursor, hour, 20);
            cursor += 20;
            *cursor++ = ' ';
            std::memcpy(cursor, endpoints[id].data(), endpoints[id].size());
            cursor += endpoints[id].size();
            *cursor++ = ' ';
            append(cursor, count, ' ');
            for (u32 value : percentiles) append(cursor, value, ' ');
            append(cursor, group.errors, ' ');
            append(cursor, static_cast<u64>(group.errors) * 1000000 / count, '\n');
        }
        output.size = static_cast<std::size_t>(cursor - output.data.get());
      }
    });
    auto close_file = [](FILE* file) { if (file) std::fclose(file); };
    std::unique_ptr<FILE, decltype(close_file)> file(std::fopen("/out/summary.txt", "wb"), close_file);
    if (!file) throw std::runtime_error("cannot open summary.txt");
    for (const auto& output : outputs)
        if (output.size && std::fwrite(output.data.get(), 1, output.size, file.get()) != output.size)
            throw std::runtime_error("cannot write summary.txt");
    if (std::fflush(file.get())) throw std::runtime_error("cannot finish summary.txt");
    return 0;
}

int main() {
    try { return solve(); }
    catch (const std::exception& error) { std::fprintf(stderr, "track-a: %s\n", error.what()); return 1; }
}
