#pragma once

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "lab_common.hpp"
#include "parallel_ranges.hpp"

namespace lab {
inline std::string readFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in)
    throw std::runtime_error("cannot open input: " + path.string());
  const auto size = in.tellg();
  if (size < 0)
    throw std::runtime_error("cannot size input: " + path.string());
  std::string result(static_cast<std::size_t>(size), '\0');
  in.seekg(0);
  if (!in.read(result.data(), static_cast<std::streamsize>(result.size())))
    throw std::runtime_error("failed reading input: " + path.string());
  return result;
}

inline std::vector<std::string_view> fields(std::string_view line) {
  std::vector<std::string_view> result;
  while (!line.empty()) {
    const auto start = line.find_first_not_of(" \t\r");
    if (start == line.npos)
      break;
    line.remove_prefix(start);
    const auto end = line.find_first_of(" \t\r");
    result.push_back(line.substr(0, end));
    if (end == line.npos)
      break;
    line.remove_prefix(end);
  }
  return result;
}

struct IndexMetrics {
  double build_seconds = 0, query_seconds = 0;
  std::uint64_t documents = 0;
  std::size_t terms = 0, postings = 0, posting_bytes = 0;
  std::size_t posting_capacity_bytes = 0, position_capacity_bytes = 0;
  std::size_t worker_threads = 1;
};
void ingest(const std::filesystem::path& input, const std::filesystem::path& output,
            std::size_t workers = practice::defaultWorkers, std::string_view algorithm = "");
void index(const std::filesystem::path& corpus, const std::filesystem::path& queries,
           const std::filesystem::path& output, IndexMetrics* metrics = nullptr,
           std::size_t workers = practice::defaultWorkers, std::string_view algorithm = "");
void join(const std::filesystem::path& records, const std::filesystem::path& lookup,
          const std::filesystem::path& output, std::string_view algorithm = "");
}
