#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
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
#include <immintrin.h>
#include <sys/mman.h>
#include <sys/stat.h>

using u32 = std::uint32_t;
using u64 = std::uint64_t;
constexpr unsigned hours = 72;
constexpr unsigned endpoint_capacity = 1842;

u64 delimiter_mask(u64 word, unsigned char delimiter) {
    const u64 bytes = word ^ (u64{delimiter} * 0x0101010101010101ULL);
    return (bytes - 0x0101010101010101ULL) & ~bytes & 0x8080808080808080ULL;
}

u64 endpoint_hash(const char*& p, const char* limit) {
    const char* start = p;
    u64 hash = 0;
    while (limit - p >= static_cast<std::ptrdiff_t>(sizeof(u64))) {
        u64 word;
        std::memcpy(&word, p, sizeof(word));
        const u64 mask = delimiter_mask(word, ' ');
        if (mask) {
            const unsigned size = std::countr_zero(mask) / 8;
            if (size) hash = _mm_crc32_u64(hash, word & ((u64{1} << (size * 8)) - 1));
            p += size;
            const u64 fingerprint = _mm_crc32_u32(static_cast<u32>(hash), static_cast<u32>(p - start));
            return fingerprint * 0x9e3779b97f4a7c15ULL;
        }
        hash = _mm_crc32_u64(hash, word);
        p += sizeof(word);
    }
    while (p < limit && *p != ' ') hash = _mm_crc32_u8(static_cast<u32>(hash), static_cast<unsigned char>(*p++));
    if (p == limit) throw std::runtime_error("missing endpoint delimiter");
    const u64 fingerprint = _mm_crc32_u32(static_cast<u32>(hash), static_cast<u32>(p - start));
    return fingerprint * 0x9e3779b97f4a7c15ULL;
}

u32 eight_digits(u64 word) {
    // Combine adjacent decimal pairs, then four-digit halves, in little-endian lanes.
    word &= 0x0f0f0f0f0f0f0f0fULL;
    word = word * 10 + (word >> 8);
    word = (word & 0x00ff00ff00ff00ffULL) * 100 + ((word >> 16) & 0x00ff00ff00ff00ffULL);
    return static_cast<u32>(word & 0xffff) * 10000 + static_cast<u32>((word >> 32) & 0xffff);
}

u32 latency(const char*& p, const char* limit) {
    u32 value = 0;
    if (limit - p >= 8) {
        u64 word;
        std::memcpy(&word, p, sizeof(word));
        const u64 mask = delimiter_mask(word, '\n');
        if (mask) {
            const unsigned size = std::countr_zero(mask) / 8;
            if (!size) throw std::runtime_error("empty latency");
            p += size;
            return eight_digits(word << ((8 - size) * 8));
        }
        value = eight_digits(word);
        p += 8;
    }
    while (*p >= '0' && *p <= '9') value = value * 10 + static_cast<unsigned>(*p++ - '0');
    return value;
}

struct Group {
    u32 count = 0;
    u32 errors = 0;
    u32 offset = 0;
};
struct __attribute__((packed)) Record {
    u32 latency;
    // Eleven endpoint bits and one error bit retain the complete uint32 latency.
    std::uint16_t key;
};
static_assert(sizeof(Record) == 6);
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
        u64 method;
        std::memcpy(&method, p, sizeof(method));
        p += std::countr_zero(delimiter_mask(method, ' ')) / 8 + 1;
        const char* endpoint = p;
        const u64 hash = endpoint_hash(p, limit);
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
        const u32 value = latency(p, limit);
        auto& records = local.records[h];
        if (records.empty()) records.reserve((end - begin) / 512 + 1);
        records.push_back({value, static_cast<std::uint16_t>(id | (u32{error} << 11))});
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

template<int distance, int upper>
__m256i exchange(__m256i values) {
    const auto peers = _mm256_permutevar8x32_epi32(values, _mm256_setr_epi32(
        0 ^ distance, 1 ^ distance, 2 ^ distance, 3 ^ distance,
        4 ^ distance, 5 ^ distance, 6 ^ distance, 7 ^ distance));
    return _mm256_blend_epi32(_mm256_min_epu32(values, peers), _mm256_max_epu32(values, peers), upper);
}

void sort_small(u32* begin, u32 count) {
    // Masked lanes cannot read the next group; UINT32_MAX padding sorts last.
    const auto mask = _mm256_cmpgt_epi32(_mm256_set1_epi32(static_cast<int>(count)),
                                       _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7));
    auto values = _mm256_blendv_epi8(_mm256_set1_epi32(-1),
                                    _mm256_maskload_epi32(reinterpret_cast<const int*>(begin), mask), mask);
    // Bitonic width/stride stages: 2/1, 4/2, 4/1, 8/4, 8/2, 8/1.
    values = exchange<1, 0x66>(values);
    values = exchange<2, 0x3c>(values);
    values = exchange<1, 0x5a>(values);
    values = exchange<4, 0xf0>(values);
    values = exchange<2, 0xcc>(values);
    values = exchange<1, 0xaa>(values);
    _mm256_maskstore_epi32(reinterpret_cast<int*>(begin), mask, values);
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
    for (Local& local : locals)
        for (u32& id : local.remap) id = ranks[id];
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
        std::vector<Group> hour_groups(endpoint_count);
        for (const Local& local : locals)
            for (const Record& record : local.records[h]) {
                Group& target = hour_groups[local.remap[record.key & 2047]];
                ++target.count;
                target.errors += record.key >> 11;
            }
        u32 offset = 0;
        std::size_t capacity = 0;
        for (u32 id = 0; id < endpoint_count; ++id) {
            hour_groups[id].offset = offset;
            offset += hour_groups[id].count;
            if (hour_groups[id].count) capacity += endpoints[id].size() + 104;
        }
        auto values = std::make_unique_for_overwrite<u32[]>(offset);
        for (Local& local : locals) {
            for (const Record& record : local.records[h])
                values[hour_groups[local.remap[record.key & 2047]].offset++] = record.latency;
            std::vector<Record>().swap(local.records[h]);
        }
        for (u32 id = 0; id < endpoint_count; ++id) hour_groups[id].offset -= hour_groups[id].count;
        const unsigned day = (h + 18) / 24;
        char hour[32];
        std::snprintf(hour, sizeof(hour), "2026-%02u-%02uT%02u:00:00Z",
                      day < 2 ? 10 : 11, day < 2 ? day + 30 : day - 1, (h + 18) % 24);
        // Include the numeric field bounds and to_chars' 20-byte writable range.
        auto& output = outputs[h];
        output.data = std::make_unique_for_overwrite<char[]>(capacity);
        char* cursor = output.data.get();
        for (u32 id = 0; id < endpoint_count; ++id) {
            Group& group = hour_groups[id];
            u32* begin = values.get() + group.offset;
            const auto count = group.count;
            if (!count) continue;
            std::array<u32, 3> percentiles;
            if (count <= 2) {
                const u32 low = std::min(begin[0], begin[count - 1]);
                const u32 high = std::max(begin[0], begin[count - 1]);
                percentiles = {low, high, high};
            } else {
                if (count <= 8) sort_small(begin, count);
                else if (count <= 32) std::sort(begin, begin + count);
                auto start = begin;
                for (unsigned j = 0; j < 3; ++j) {
                    constexpr unsigned ranks[]{50, 95, 99};
                    auto rank = begin + (ranks[j] * count + 99) / 100 - 1;
                    if (count > 32) {
                        // For 33..99 samples, p99 is the maximum by nearest rank:
                        // ceil(99*count/100) == count. Avoid a final nth_element.
                        if (j == 2 && count < 100) {
                            percentiles[j] = *std::max_element(start, begin + count);
                            break;
                        }
                        std::nth_element(start, rank, begin + count);
                    }
                    percentiles[j] = *rank;
                    start = rank;
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
