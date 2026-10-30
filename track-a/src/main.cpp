#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>
#include <sys/stat.h>

using u32 = std::uint32_t;
using u64 = std::uint64_t;
constexpr unsigned hours = 72;

struct Hash {
    using is_transparent = void;
    std::size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
};
using Ids = std::unordered_map<std::string, u32, Hash, std::equal_to<>>;

struct Group {
    std::vector<u32> latencies;
    u32 errors = 0;
    u32 key = 0;
};
struct Local {
    std::array<u64, 4096> ids{};
    std::vector<std::string> endpoints;
    std::vector<u32> slots;
    std::vector<Group> groups;
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

void parse(Local& local, std::size_t begin, std::size_t end) {
    auto close_file = [](FILE* file) { if (file) std::fclose(file); };
    std::unique_ptr<FILE, decltype(close_file)> file(std::fopen("/data/requests.log", "rb"), close_file);
    if (!file) throw std::runtime_error("cannot open requests.log");
    std::vector<char> buffer(256 * 1024);
    std::size_t position = begin;
    if (begin) {
        if (fseeko(file.get(), static_cast<off_t>(begin - 1), SEEK_SET))
            throw std::runtime_error("cannot seek requests.log");
        if (std::fgetc(file.get()) != '\n') {
            int c;
            do {
                c = std::fgetc(file.get());
                if (c == EOF) return;
                ++position;
            } while (c != '\n');
        }
    }
    local.endpoints.reserve(1842);
    local.groups.reserve(16384);
    const auto row = [&](const char* p) {
        const unsigned month = (p[5] - '0') * 10 + p[6] - '0';
        const unsigned day = (p[8] - '0') * 10 + p[9] - '0';
        const unsigned hour = (p[11] - '0') * 10 + p[12] - '0';
        const unsigned h = (month == 10 ? day - 30 : day + 1) * 24 + hour - 18;
        p += 28;
        while (*p++ != ' ') {}
        const char* endpoint = p;
        while (*p != ' ') ++p;
        const std::string_view name(endpoint, p - endpoint);
        const u64 hash = std::hash<std::string_view>{}(name);
        auto index = hash & (local.ids.size() - 1);
        unsigned probes = 0;
        while (local.ids[index]) {
            const u32 candidate = static_cast<u32>(local.ids[index]) - 1;
            if ((local.ids[index] >> 32) == (hash >> 32) && local.endpoints[candidate] == name) break;
            index = (index + 1) & (local.ids.size() - 1);
            if (++probes == local.ids.size()) throw std::runtime_error("endpoint catalogue exceeds capacity");
        }
        u32 id;
        if (!local.ids[index]) {
            id = static_cast<u32>(local.endpoints.size());
            local.ids[index] = (hash & 0xffffffff00000000ULL) | (u64{id} + 1);
            local.endpoints.emplace_back(name);
            local.slots.resize(local.slots.size() + hours, UINT32_MAX);
        } else id = static_cast<u32>(local.ids[index]) - 1;
        const bool error = p[1] == '5';
        p += 5;
        u32 latency = 0;
        while (*p >= '0' && *p <= '9') latency = latency * 10 + static_cast<unsigned>(*p++ - '0');
        const u32 key = id * hours + h;
        u32& slot = local.slots[key];
        if (slot == UINT32_MAX) {
            slot = static_cast<u32>(local.groups.size());
            local.groups.push_back({{}, 0, key});
            local.groups.back().latencies.reserve(4);
        }
        Group& group = local.groups[slot];
        group.latencies.push_back(latency);
        group.errors += error;
    };
    std::size_t carry = 0;
    while (position < end) {
        const auto got = std::fread(buffer.data() + carry, 1, buffer.size() - carry, file.get());
        if (!got) throw std::runtime_error("truncated requests.log");
        const char* p = buffer.data();
        const char* stop = p + carry + got;
        while (position < end) {
            const char* newline = static_cast<const char*>(std::memchr(p, '\n', stop - p));
            if (!newline) break;
            row(p);
            position += static_cast<std::size_t>(newline + 1 - p);
            p = newline + 1;
        }
        carry = static_cast<std::size_t>(stop - p);
        if (carry == buffer.size()) throw std::runtime_error("request line exceeds buffer");
        std::memmove(buffer.data(), p, carry);
    }
    if (std::ferror(file.get())) throw std::runtime_error("cannot read requests.log");
}

void append(std::string& output, u64 value, char separator) {
    char text[24];
    auto result = std::to_chars(text, text + sizeof(text), value);
    output.append(text, result.ptr);
    output += separator;
}

int solve() {
    struct stat status{};
    if (stat("/data/requests.log", &status)) throw std::runtime_error("cannot stat requests.log");
    const auto size = static_cast<std::size_t>(status.st_size);
    const unsigned workers = size < 1024 * 1024 ? 1 : 12;
    std::vector<Local> locals(workers);
    parallel(workers, [&](unsigned w) { parse(locals[w], size * w / workers, size * (w + 1) / workers); });
    Ids ids;
    std::vector<std::string> endpoints;
    std::vector<Group> groups;
    for (Local& local : locals) {
        std::vector<u32> remap(local.endpoints.size());
        for (u32 i = 0; i < remap.size(); ++i) {
            auto [it, inserted] = ids.try_emplace(local.endpoints[i], static_cast<u32>(endpoints.size()));
            if (inserted) {
                endpoints.push_back(local.endpoints[i]);
                groups.resize(endpoints.size() * hours);
            }
            remap[i] = it->second;
        }
        for (Group& group : local.groups) {
            Group& target = groups[remap[group.key / hours] * hours + group.key % hours];
            target.errors += group.errors;
            if (target.latencies.empty()) target.latencies = std::move(group.latencies);
            else target.latencies.insert(target.latencies.end(), group.latencies.begin(), group.latencies.end());
        }
        local = Local{};
    }
    std::vector<u32> order(endpoints.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](u32 a, u32 b) { return endpoints[a] < endpoints[b]; });
    std::array<std::string, hours> outputs;
    std::atomic<unsigned> next{0};
    parallel(workers, [&](unsigned) {
      for (;;) {
        const unsigned h = next.fetch_add(1, std::memory_order_relaxed);
        if (h >= hours) break;
        const unsigned day = (h + 18) / 24;
        char hour[32];
        std::snprintf(hour, sizeof(hour), "2026-%02u-%02uT%02u:00:00Z",
                      day < 2 ? 10 : 11, day < 2 ? day + 30 : day - 1, (h + 18) % 24);
        auto& output = outputs[h];
        output.reserve(endpoints.size() * 96);
        for (u32 id : order) {
            Group& group = groups[id * hours + h];
            auto& values = group.latencies;
            const auto count = values.size();
            if (!count) continue;
            std::array<u32, 3> percentiles;
            if (count <= 32) std::sort(values.begin(), values.end());
            auto start = values.begin();
            for (unsigned j = 0; j < 3; ++j) {
                constexpr unsigned ranks[]{50, 95, 99};
                auto rank = values.begin() + (ranks[j] * count + 99) / 100 - 1;
                if (count > 32) std::nth_element(start, rank, values.end());
                percentiles[j] = *rank;
                start = rank;
            }
            output += hour;
            output += ' ';
            output += endpoints[id];
            output += ' ';
            append(output, count, ' ');
            for (u32 value : percentiles) append(output, value, ' ');
            append(output, group.errors, ' ');
            append(output, static_cast<u64>(group.errors) * 1000000 / count, '\n');
        }
      }
    });
    auto close_file = [](FILE* file) { if (file) std::fclose(file); };
    std::unique_ptr<FILE, decltype(close_file)> file(std::fopen("/out/summary.txt", "wb"), close_file);
    if (!file) throw std::runtime_error("cannot open summary.txt");
    for (const auto& output : outputs)
        if (std::fwrite(output.data(), 1, output.size(), file.get()) != output.size())
            throw std::runtime_error("cannot write summary.txt");
    if (std::fflush(file.get())) throw std::runtime_error("cannot finish summary.txt");
    return 0;
}

int main() {
    try { return solve(); }
    catch (const std::exception& error) { std::fprintf(stderr, "track-a: %s\n", error.what()); return 1; }
}
