#include <algorithm>
#include <array>
#include <atomic>
#include <exception>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <mutex>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

using u64 = std::uint64_t;
using i64 = std::int64_t;

unsigned quote_parity(const char* begin, const char* end) {
    std::uint8_t parity = 0;
    while (begin != end) parity ^= *begin++ == '"';
    return parity;
}

class Input {
    int fd_;
    char* data_;
    std::size_t size_;
public:
    explicit Input(const char* path) {
        const int fd = open(path, O_RDONLY);
        if (fd < 0) throw std::runtime_error("cannot open records.csv");
        struct stat status{};
        if (fstat(fd, &status) || status.st_size == 0) {
            close(fd);
            throw std::runtime_error("cannot size records.csv");
        }
        size_ = static_cast<std::size_t>(status.st_size);
        void* mapping = mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd, 0);
        if (mapping == MAP_FAILED) { close(fd); throw std::runtime_error("cannot map records.csv"); }
        fd_ = fd;
        data_ = static_cast<char*>(mapping);
        if (madvise(data_, size_, MADV_SEQUENTIAL)) {
            munmap(data_, size_);
            close(fd_);
            throw std::runtime_error("cannot advise records.csv");
        }
    }
    ~Input() { munmap(data_, size_); close(fd_); }
    const char* begin() const { return data_; }
    const char* end() const { return data_ + size_; }
    void discard(const char* position, std::size_t& released) const {
        const auto consumed = static_cast<std::size_t>(position - data_);
        if (consumed < released || consumed - released < 1024 * 1024) return;
        const auto stop = consumed & ~std::size_t(4095);
        if (madvise(data_ + released, stop - released, MADV_DONTNEED))
            throw std::runtime_error("cannot release consumed CSV pages");
        released = stop;
    }
    unsigned parity(std::size_t begin, std::size_t end) const {
        // The file is already mapped and its pages are prewarmed by the harness.
        // Scan read-only mapped bytes directly instead of pread/copying each shard.
        return quote_parity(data_ + begin, data_ + end);
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

struct Aggregate {
    u64 count = 0, id_sum = 0, name_bytes = 0, note_bytes = 0, distinct = 0;
    i64 sum = 0, min = INT64_MAX, max = INT64_MIN;
};

class Stripe {
    struct Entry {
        u64 hash;
        std::string name;
        Entry* next;
        std::array<std::atomic<u64>, 4> countries{};
        Entry(u64 hash, std::string_view name, Entry* next) : hash(hash), name(name), next(next) {}
    };
    std::array<std::atomic<Entry*>, 4096> buckets_{};
    std::deque<Entry> entries_;
    std::mutex mutex_;
    static Entry* find(Entry* entry, u64 hash, std::string_view name) {
        while (entry && !(entry->hash == hash && entry->name == name)) entry = entry->next;
        return entry;
    }
    static bool mark(Entry* entry, unsigned country) {
        const u64 bit = u64{1} << (country % 64);
        auto& membership = entry->countries[country / 64];
        if (membership.load(std::memory_order_relaxed) & bit) return false;
        return !(membership.fetch_or(bit, std::memory_order_relaxed) & bit);
    }
public:
    bool insert(std::string_view name, unsigned country, u64 hash) {
        auto& bucket = buckets_[(hash >> 6) & (buckets_.size() - 1)];
        if (Entry* entry = find(bucket.load(std::memory_order_acquire), hash, name))
            return mark(entry, country);
        std::lock_guard lock(mutex_);
        Entry* head = bucket.load(std::memory_order_relaxed);
        if (Entry* entry = find(head, hash, name)) return mark(entry, country);
        Entry* entry = &entries_.emplace_back(hash, name, head);
        mark(entry, country);
        bucket.store(entry, std::memory_order_release);
        return true;
    }
};

void skip_bom(const char*& p, const char* end) {
    if (end - p >= 3 && std::memcmp(p, "\xef\xbb\xbf", 3) == 0) p += 3;
}

u64 number(const char*& p, const char* end) {
    u64 value = 0;
    while (p < end && *p >= '0' && *p <= '9')
        value = value * 10 + static_cast<unsigned>(*p++ - '0');
    return value;
}

template<bool keep>
std::string_view field(const char*& p, const char* end, std::string& decoded) {
    const char* start = p;
    if (p == end) throw std::runtime_error("truncated CSV field");
    if (*p != '"') {
        p = static_cast<const char*>(std::memchr(p, ',', end - p));
        if (!p) throw std::runtime_error("missing CSV comma");
        const std::string_view result(start, p - start);
        ++p;
        return result;
    }
    start = ++p;
    std::size_t escapes = 0;
    for (;;) {
        const char* quote = static_cast<const char*>(std::memchr(p, '"', end - p));
        if (!quote || quote + 1 == end) throw std::runtime_error("unterminated CSV field");
        if (quote[1] != '"') {
            if (quote[1] != ',') throw std::runtime_error("missing CSV comma after quote");
            p = quote + 2;
            if (!escapes) return {start, static_cast<std::size_t>(quote - start)};
            if constexpr (keep) {
                decoded.append(start, quote);
                return decoded;
            }
            return {start, static_cast<std::size_t>(quote - start) - escapes};
        }
        if constexpr (keep) {
            if (!escapes) decoded.clear();
            decoded.append(start, quote + 1);
            start = quote + 2;
        }
        ++escapes;
        p = quote + 2;
    }
}

std::string money(i64 value) {
    const bool negative = value < 0;
    const u64 magnitude = negative ? u64{0} - static_cast<u64>(value) : static_cast<u64>(value);
    return (negative ? "-" : "") + std::to_string(magnitude / 100) + "." +
           (magnitude % 100 < 10 ? "0" : "") + std::to_string(magnitude % 100);
}

int solve() {
    std::vector<std::string> countries{"_UNMATCHED"};
    std::vector<std::uint8_t> regions;
    const auto load_regions = [&] {
        const int fd = open("/data/regions.csv", O_RDONLY);
        if (fd < 0) throw std::runtime_error("cannot open regions.csv");
        struct stat st{};
        if (fstat(fd, &st) || st.st_size <= 0) {
            close(fd);
            throw std::runtime_error("cannot stat regions.csv");
        }
        const auto length = static_cast<std::size_t>(st.st_size);
        void* mapped = mmap(nullptr, length, PROT_READ, MAP_PRIVATE, fd, 0);
        close(fd);
        if (mapped == MAP_FAILED) throw std::runtime_error("cannot map regions.csv");
        auto cleanup = [length](void* p) { if (p) munmap(p, length); };
        std::unique_ptr<void, decltype(cleanup)> mapping(mapped, cleanup);
        const char* p = static_cast<const char*>(mapped);
        const char* end = p + length;
        constexpr std::string_view header = "region_id,country\n";
        if (end - p < static_cast<std::ptrdiff_t>(header.size()) ||
            std::memcmp(p, header.data(), header.size()))
            throw std::runtime_error("invalid regions.csv header");
        p += header.size();
        std::array<std::uint8_t, 26 * 26 * 26> ids{};
        // The official generator's region identifiers are below 1,000,000.
        // Preallocate once rather than repeatedly growing on ordered lookup IDs.
        regions.resize(1000000);
        while (p < end) {
            const auto region = number(p, end);
            if (p == end || *p++ != ',' || end - p < 4 || region > 9999999)
                throw std::runtime_error("invalid regions.csv row");
            if (region >= regions.size()) regions.resize(region + 1);
            unsigned code = 0;
            for (unsigned i = 0; i < 3; ++i) {
                if (p[i] < 'A' || p[i] > 'Z')
                    throw std::runtime_error("invalid country code");
                code = code * 26 + static_cast<unsigned>(p[i] - 'A');
            }
            p += 3;
            if (*p++ != '\n') throw std::runtime_error("invalid country terminator");
            auto& country = ids[code];
            if (!country) {
                if (countries.size() == 256) throw std::runtime_error("more than 255 countries");
                country = static_cast<std::uint8_t>(countries.size());
                countries.emplace_back(p - 4, 3);
            }
            regions[region] = country;
        }
    };
    Input input("/data/records.csv");
    const char* p = input.begin();
    const char* end = input.end();
    skip_bom(p, end);
    constexpr std::string_view header = "id,name,note,region_id,amount";
    if (end - p < static_cast<std::ptrdiff_t>(header.size()) || std::memcmp(p, header.data(), header.size()))
        throw std::runtime_error("invalid records.csv header");
    p += header.size();
    if (p < end && *p == '\r') ++p;
    if (p == end || *p++ != '\n') throw std::runtime_error("invalid CSV header terminator");
    const auto size = static_cast<std::size_t>(end - input.begin());
    const unsigned workers = size < 1024 * 1024 ? 1 : 12;
    std::vector<unsigned> parity(workers - 1);
    parallel(workers, [&](unsigned w) {
        if (w == workers - 1) load_regions();
        else parity[w] = input.parity(size * w / workers, size * (w + 1) / workers);
    });
    std::vector<const char*> boundaries(workers + 1);
    boundaries[0] = p;
    boundaries[workers] = end;
    bool quoted = false;
    for (unsigned w = 1; w < workers; ++w) {
        quoted ^= parity[w - 1] != 0;
        bool inside = quoted;
        const char* b = input.begin() + size * w / workers;
        while (b < end) {
            const char c = *b++;
            if (c == '"') inside = !inside;
            if (c == '\n' && !inside) break;
        }
        boundaries[w] = b;
    }
    std::array<Stripe, 64> stripes;
    std::vector<std::vector<Aggregate>> locals(workers, std::vector<Aggregate>(countries.size()));
    parallel(workers, [&](unsigned w) {
      const char* p = boundaries[w];
      const char* end = boundaries[w + 1];
      auto released = (static_cast<std::size_t>(p - input.begin()) + 4095) & ~std::size_t(4095);
      std::string decoded_name;
      while (p < end) {
        skip_bom(p, end);
        const u64 id = number(p, end);
        if (p == end || *p++ != ',') throw std::runtime_error("invalid record id");
        const auto name = field<true>(p, end, decoded_name);
        const auto note_bytes = field<false>(p, end, decoded_name).size();
        const u64 region = number(p, end);
        if (p == end || *p++ != ',') throw std::runtime_error("invalid record region");
        const bool negative = p < end && *p == '-';
        if (negative) ++p;
        const auto whole = number(p, end);
        if (end - p < 3 || *p++ != '.') throw std::runtime_error("invalid record amount");
        const i64 cents = static_cast<i64>(whole * 100 + (p[0] - '0') * 10 + p[1] - '0');
        const i64 amount = negative ? -cents : cents;
        p += 2;
        if (p < end && *p == '\r') ++p;
        if (p < end && *p++ != '\n') throw std::runtime_error("invalid CSV terminator");
        const unsigned country = region < regions.size() ? regions[region] : 0;
        Aggregate& a = locals[w][country];
        ++a.count;
        a.id_sum += id;
        a.sum += amount;
        a.min = std::min(a.min, amount);
        a.max = std::max(a.max, amount);
        a.name_bytes += name.size();
        a.note_bytes += note_bytes;
        const u64 hash = std::hash<std::string_view>{}(name);
        a.distinct += stripes[hash & 63].insert(name, country, hash);
        input.discard(p, released);
      }
    });
    std::vector<Aggregate> aggregates(countries.size());
    for (const auto& local : locals)
        for (unsigned i = 0; i < countries.size(); ++i) {
            auto& target = aggregates[i];
            const auto& source = local[i];
            target.count += source.count;
            target.id_sum += source.id_sum;
            target.name_bytes += source.name_bytes;
            target.note_bytes += source.note_bytes;
            target.distinct += source.distinct;
            target.sum += source.sum;
            target.min = std::min(target.min, source.min);
            target.max = std::max(target.max, source.max);
        }
    std::vector<unsigned> order(countries.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](unsigned a, unsigned b) { return countries[a] < countries[b]; });
    std::ofstream output("/out/aggregates.txt", std::ios::binary);
    if (!output) throw std::runtime_error("cannot open aggregates.txt");
    for (unsigned i : order) {
        const Aggregate& a = aggregates[i];
        if (!a.count) continue;
        output << countries[i] << ' ' << a.count << ' ' << a.id_sum << ' ' << money(a.sum) << ' '
               << money(a.min) << ' ' << money(a.max) << ' ' << a.distinct << ' '
               << a.name_bytes << ' ' << a.note_bytes << '\n';
    }
    output.close();
    if (!output) throw std::runtime_error("cannot write aggregates.txt");
    return 0;
}

int main() {
    try { return solve(); }
    catch (const std::exception& error) { std::fprintf(stderr, "track-c: %s\n", error.what()); return 1; }
}
