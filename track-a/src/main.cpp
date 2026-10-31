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

std::pair<const char*, u64> endpoint_scan_hash(const char* p, const char* limit) {
    const char* start = p;
    u64 hash = 0x9e3779b97f4a7c15ULL;
    while (limit - p >= 8) {
        u64 word;
        std::memcpy(&word, p, 8);
        const u64 check = word ^ 0x2020202020202020ULL;
        const u64 spaces = (check - 0x0101010101010101ULL) & ~check & 0x8080808080808080ULL;
        if (spaces) break;
        hash = (hash ^ word) * 0x9e3779b97f4a7c15ULL;
        hash ^= hash >> 29;
        p += 8;
    }
    while (p < limit && *p != ' ')
        hash = (hash ^ static_cast<unsigned char>(*p++)) * 0x100000001b3ULL;
    if (p == limit) throw std::runtime_error("missing endpoint delimiter");
    hash ^= u64(p - start) * 0x9e3779b97f4a7c15ULL;
    return {p, hash ^ (hash >> 32)};
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
        const auto [stop, hash] = endpoint_scan_hash(p, limit);
        p = stop;
        const std::string_view name(endpoint, p - endpoint);
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
            if (count <= 32) {
                std::sort(begin, begin + count);
                percentiles = {begin[(50 * count + 99) / 100 - 1],
                               begin[(95 * count + 99) / 100 - 1],
                               begin[(99 * count + 99) / 100 - 1]};
            } else {
                u32* median = begin + (50 * count + 99) / 100 - 1;
                std::nth_element(begin, median, begin + count);
                percentiles[0] = *median;
                u32* p95 = begin + (95 * count + 99) / 100 - 1;
                std::nth_element(median + 1, p95, begin + count);
                percentiles[1] = *p95;
                if (count < 100) percentiles[2] = *std::max_element(p95 + 1, begin + count);
                else if (count == 100) {
                    std::nth_element(p95 + 1, begin + 98, begin + count);
                    percentiles[2] = begin[98];
                } else {
                    u32* p99 = begin + (99 * count + 99) / 100 - 1;
                    std::nth_element(p95 + 1, p99, begin + count);
                    percentiles[2] = *p99;
                }
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
