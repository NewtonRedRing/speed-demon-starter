#include <charconv>
#include <iostream>
#include <stdexcept>
#include <string_view>

#include "track_a.hpp"

namespace {

std::size_t parseThreadCount(std::string_view text) {
  std::size_t value = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (text.empty() || error != std::errc{} || end != text.data() + text.size() || value == 0 ||
      value > 12)
    throw std::runtime_error("threads must be an integer in 1..12");
  return value;
}

}

int main(int argc, char** argv) {
  try {
    if (argc < 3)
      throw std::runtime_error("usage: speeddemon_a /data /out [--threads N]");

    std::size_t workers = 8;
    bool threads_set = false;
    for (int i = 3; i < argc; ++i) {
      if (std::string_view(argv[i]) != "--threads" || threads_set || i + 1 >= argc)
        throw std::runtime_error("expected one --threads N option");
      workers = parseThreadCount(argv[++i]);
      threads_set = true;
    }

    lab::ingest(argv[1], argv[2], workers, "radix");
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "speeddemon_a: " << error.what() << '\n';
    return 1;
  }
}
    std::string line;

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
    for (unsigned h = 0; h < 72; ++h) {
        for (const std::string& endpoint : endpoints) {
            const auto found = ids.find(endpoint);
            Group& group = groups[h * 1842 + found->second];
            const std::size_t count = group.latencies.size();
            if (count == 0) continue;
            std::sort(group.latencies.begin(), group.latencies.end());
            const auto percentile = [&](std::size_t p) {
                return group.latencies[(p * count + 99) / 100 - 1];
            };
            const std::uint64_t ppm = static_cast<std::uint64_t>(group.errors) * 1000000 / count;
            output << hours[h] << ' ' << endpoint << ' ' << count << ' '
                   << percentile(50) << ' ' << percentile(95) << ' ' << percentile(99) << ' '
                   << group.errors << ' ' << ppm << '\n';
        }
    }
    return output ? 0 : 1;
}
