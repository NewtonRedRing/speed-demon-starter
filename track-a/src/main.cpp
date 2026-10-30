#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <numeric>

struct TransparentHash {
    using is_transparent = void;
    std::size_t operator()(std::string_view s) const noexcept {
        return std::hash<std::string_view>{}(s);
    }
};

struct TransparentEqual {
    using is_transparent = void;
    bool operator()(std::string_view a, std::string_view b) const noexcept {
        return a == b;
    }
};

struct Group {
    std::vector<std::uint32_t> latencies;
    std::uint32_t errors = 0;
};

std::uint32_t number(std::string_view s) {
    std::uint32_t value = 0;
    for (char c : s) value = value * 10 + static_cast<unsigned>(c - '0');
    return value;
}

bool byte_less(const std::string& a, const std::string& b) {
    return std::lexicographical_compare(
        a.begin(), a.end(), b.begin(), b.end(),
        [](unsigned char x, unsigned char y) { return x < y; });
}

inline std::uint32_t percentile_value(const std::vector<std::uint32_t>& latencies, std::size_t p) {
    const std::size_t count = latencies.size();
    return latencies[(p * count + 99) / 100 - 1];
}

int main() {
    std::ios::sync_with_stdio(false);
    
    std::ifstream input("/data/requests.log", std::ios::binary);
    if (!input) return 1;
    
    std::unordered_map<std::string, std::uint16_t, TransparentHash, TransparentEqual> ids;
    ids.reserve(2048);
    std::vector<std::string> endpoints;
    endpoints.reserve(1842);
    std::vector<Group> groups(72 * 1842);
    std::string line;
    line.reserve(512);

    while (std::getline(input, line)) {
        const unsigned month = number(std::string_view(line).substr(5, 2));
        const unsigned day = number(std::string_view(line).substr(8, 2));
        const unsigned hour = number(std::string_view(line).substr(11, 2));
        const unsigned hour_index = (month == 10 ? day - 30 : day + 1) * 24 + hour - 18;
        const std::size_t method_end = line.find(' ', 28);
        const std::size_t endpoint_begin = method_end + 1;
        const std::size_t endpoint_end = line.find(' ', endpoint_begin);
        const std::string_view endpoint(line.data() + endpoint_begin, endpoint_end - endpoint_begin);

        auto it = ids.find(endpoint);
        std::uint16_t endpoint_id;
        if (it == ids.end()) {
            endpoint_id = static_cast<std::uint16_t>(endpoints.size());
            endpoints.emplace_back(endpoint);
            ids.emplace(endpoints.back(), endpoint_id);
        } else {
            endpoint_id = it->second;
        }

        const std::size_t status_begin = endpoint_end + 1;
        const std::size_t status_end = line.find(' ', status_begin);
        const unsigned status = number(std::string_view(line).substr(status_begin, status_end - status_begin));
        const auto latency = number(std::string_view(line).substr(status_end + 1));
        Group& group = groups[hour_index * 1842 + endpoint_id];
        group.latencies.push_back(latency);
        group.errors += status >= 500 && status <= 599;
    }

    if (input.bad()) return 1;
    
    std::sort(endpoints.begin(), endpoints.end(), byte_less);
    
    std::array<std::string, 72> hours;
    for (unsigned h = 0; h < 72; ++h) {
        const unsigned day = (h + 18) / 24;
        const unsigned month = day < 2 ? 10 : 11;
        const unsigned date = day == 0 ? 30 : day == 1 ? 31 : day == 2 ? 1 : 2;
        const unsigned hour = (h + 18) % 24;
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "2026-%02u-%02uT%02u:00:00Z", month, date, hour);
        hours[h] = buffer;
    }

    std::ofstream output("/out/summary.txt", std::ios::binary);
    if (!output) return 1;
    
    output.rdbuf()->pubsetbuf(nullptr, 1024 * 1024);
    
    for (unsigned h = 0; h < 72; ++h) {
        for (const std::string& endpoint : endpoints) {
            Group& group = groups[h * 1842 + ids[endpoint]];
            const std::size_t count = group.latencies.size();
            if (count == 0) continue;
            
            std::sort(group.latencies.begin(), group.latencies.end());
            
            const std::uint32_t p50 = percentile_value(group.latencies, 50);
            const std::uint32_t p95 = percentile_value(group.latencies, 95);
            const std::uint32_t p99 = percentile_value(group.latencies, 99);
            const std::uint64_t ppm = static_cast<std::uint64_t>(group.errors) * 1000000 / count;
            
            output << hours[h] << ' ' << endpoint << ' ' << count << ' '
                   << p50 << ' ' << p95 << ' ' << p99 << ' '
                   << group.errors << ' ' << ppm << '\n';
        }
    }
    return output ? 0 : 1;
}
