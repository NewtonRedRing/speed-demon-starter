#include <algorithm>
#include <array>
#include <cstring>
#include <exception>
#include <sstream>
#include <unordered_map>

#include "lab.hpp"

namespace lab {
namespace {
class CsvReader {
  std::ifstream input_;
  std::array<char, 262144> buffer_{};
  std::size_t cursor_ = 0, size_ = 0;
  std::size_t consumed_ = 0;
  std::size_t begin_ = 0;
  std::size_t end_ = std::numeric_limits<std::size_t>::max();

  int peek(std::size_t ahead = 0) {
    if (cursor_ + ahead >= size_) {
      const auto remaining = size_ - cursor_;
      if (cursor_ != 0)
        std::move(buffer_.begin() + static_cast<std::ptrdiff_t>(cursor_),
                  buffer_.begin() + static_cast<std::ptrdiff_t>(size_), buffer_.begin());
      input_.read(buffer_.data() + remaining,
                  static_cast<std::streamsize>(buffer_.size() - remaining));
      if (input_.bad())
        throw std::runtime_error("failed reading CSV");
      const auto got = static_cast<std::size_t>(input_.gcount());
      size_ = remaining + got;
      consumed_ += got;
      cursor_ = 0;
    }
    return cursor_ + ahead < size_ ? static_cast<unsigned char>(buffer_[cursor_ + ahead]) : -1;
  }
  int take() {
    const auto result = peek();
    if (result != -1)
      ++cursor_;
    return result;
  }

 public:
  explicit CsvReader(const std::filesystem::path& path) : input_(path, std::ios::binary) {
    if (!input_)
      throw std::runtime_error("cannot open CSV: " + path.string());
  }
  CsvReader(const std::filesystem::path& path, std::size_t begin, std::size_t end)
      : input_(path, std::ios::binary) {
    if (!input_)
      throw std::runtime_error("cannot open CSV: " + path.string());
    input_.seekg(static_cast<std::streamoff>(begin));
    begin_ = begin;
    end_ = end;
  }
  std::size_t position() const {
    return begin_ + consumed_ - (size_ - cursor_);
  }
  bool exhausted() const {
    return position() >= end_;
  }
  bool next(std::vector<std::string>& row, bool project_records = false) {
    if (exhausted())
      return false;
    if (peek() == 0xEF && peek(1) == 0xBB && peek(2) == 0xBF) {
      take();
      take();
      take();
    }
    if (peek() == -1)
      return false;
    std::size_t column = 0;
    for (;;) {
      if (column == row.size())
        row.emplace_back();
      const bool keep = !project_records || column == 0 || column >= 3;
      auto& field = row[column++];
      field.clear();
      if (peek() == '"') {
        take();
        bool closed = false;
        while (peek() != -1) {
          const auto* start = buffer_.data() + cursor_;
          const auto* quote = static_cast<const char*>(std::memchr(start, '"', size_ - cursor_));
          const auto length = quote ? static_cast<std::size_t>(quote - start) : size_ - cursor_;
          if (keep)
            field.append(start, length);
          cursor_ += length;
          if (!quote)
            continue;
          ++cursor_;
          if (peek() == '"') {
            if (keep)
              field += '"';
            take();
          } else {
            closed = true;
            break;
          }
        }
        if (!closed)
          throw std::runtime_error("unterminated CSV quote");
      } else {
        while (peek() != -1) {
          const auto start = cursor_;
          while (cursor_ < size_) {
            const auto c = buffer_[cursor_];
            if (c == ',' || c == '\r' || c == '\n' || c == '"')
              break;
            ++cursor_;
          }
          if (keep)
            field.append(buffer_.data() + start, cursor_ - start);
          if (cursor_ != size_) {
            if (buffer_[cursor_] == '"')
              throw std::runtime_error("quote inside unquoted CSV field");
            break;
          }
        }
      }
      const auto complete = [&] {
        row.resize(column);
        return true;
      };
      const auto separator = take();
      if (separator == -1)
        return complete();
      if (separator == ',')
        continue;
      if (separator == '\n')
        return complete();
      if (separator == '\r' && peek() == '\n') {
        take();
        return complete();
      }
      throw std::runtime_error("invalid CSV separator");
    }
  }
};

std::int64_t parseCents(std::string_view value) {
  const bool negative = value.starts_with('-');
  if (negative)
    value.remove_prefix(1);
  const auto dot = value.find('.');
  if (dot == value.npos || dot == 0 || dot + 3 != value.size())
    throw std::runtime_error("practice formatMoney needs exactly two decimal places");
  const auto whole = integer<std::uint64_t>(value.substr(0, dot));
  const auto fraction = integer<std::uint64_t>(value.substr(dot + 1));
  const auto limit =
      static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + (negative ? 1U : 0U);
  if (whole > limit / 100 || (whole == limit / 100 && fraction > limit % 100))
    throw std::runtime_error("formatMoney outside int64 parseCents");
  const auto magnitude = whole * 100 + fraction;
  if (negative && magnitude == limit)
    return std::numeric_limits<std::int64_t>::min();
  const auto result = static_cast<std::int64_t>(magnitude);
  return negative ? -result : result;
}

std::string formatMoney(std::int64_t value) {
  const bool negative = value < 0;
  const auto magnitude = negative ? std::uint64_t{0} - static_cast<std::uint64_t>(value)
                                  : static_cast<std::uint64_t>(value);
  const auto fraction = magnitude % 100;
  return std::string(negative ? "-" : "") + std::to_string(magnitude / 100) + "." +
         (fraction < 10 ? "0" : "") + std::to_string(fraction);
}

std::string formatCsvField(std::string_view field) {
  if (field.find_first_of(",\"\r\n") == field.npos)
    return std::string(field);
  std::string result = "\"";
  for (char c : field) {
    result += c;
    if (c == '"')
      result += '"';
  }
  return result + '"';
}
}

void join(const std::filesystem::path& records, const std::filesystem::path& lookup,
          const std::filesystem::path& output, std::string_view algorithm) {
  if (algorithm.empty())
    algorithm = "serial";
  if (algorithm != "serial")
    throw std::runtime_error("C algorithm must be serial");
  const auto workers = practice::defaultWorkers;
  CsvReader lookup_csv(lookup);
  std::vector<std::string> row;
  if (!lookup_csv.next(row) || row != std::vector<std::string>{"region_id", "region"})
    throw std::runtime_error("invalid practice lookup header");
  struct WideCents {
    std::uint64_t low = 0;
    std::int64_t high = 0;
    void add(std::int64_t amount) {
      const auto before = low;
      low += static_cast<std::uint64_t>(amount);
      if (amount < 0 && low >= before)
        --high;
      if (amount >= 0 && low < before)
        ++high;
    }
    std::int64_t checked() const {
      constexpr auto max = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
      if (high == 0 && low <= max)
        return static_cast<std::int64_t>(low);
      if (high == -1 && low > max) {
        const auto magnitude = std::uint64_t{0} - low;
        if (magnitude == max + 1)
          return std::numeric_limits<std::int64_t>::min();
        return -static_cast<std::int64_t>(magnitude);
      }
      throw std::runtime_error("aggregate parseCents overflow");
    }
  };
  struct Aggregate {
    std::string_view region;
    std::uint64_t count = 0;
    WideCents total;
  };
  struct Region {
    std::string name;
    std::size_t group = 0;
  };
  std::vector<Aggregate> aggregates;
  std::unordered_map<std::uint32_t, Region> regions;
  auto min_region_id = std::numeric_limits<std::uint32_t>::max();
  std::uint32_t max_region_id = 0;
  while (lookup_csv.next(row)) {
    if (row.size() != 2)
      throw std::runtime_error("lookup must contain two columns");
    const auto id = integer<std::uint32_t>(row[0]);
    if (!regions.try_emplace(id, Region{row[1]}).second)
      throw std::runtime_error("duplicate lookup region id");
    min_region_id = std::min(min_region_id, id);
    max_region_id = std::max(max_region_id, id);
  }
  std::vector<Region*> dense_regions;
  if (!regions.empty()) {
    const auto range = static_cast<std::uint64_t>(max_region_id) - min_region_id + 1;
    constexpr std::size_t max_dense_bytes = 8 * 1024 * 1024;
    if (range <= regions.size() * 4ULL && range <= max_dense_bytes / sizeof(Region*)) {
      dense_regions.resize(static_cast<std::size_t>(range));
      for (auto& [id, region] : regions)
        dense_regions[id - min_region_id] = &region;
    }
  }
  std::unordered_map<std::string_view, std::size_t> names;
  for (auto& entry : regions) {
    auto& region = entry.second;
    const auto [group, inserted] = names.try_emplace(region.name, aggregates.size());
    if (inserted)
      aggregates.push_back({group->first, 0, 0});
    region.group = group->second;
  }
  constexpr std::size_t kShards = 24;
  constexpr std::size_t kBlocks = 64;
  const auto record_bytes = static_cast<std::size_t>(std::filesystem::file_size(records));
  const auto block_bytes = std::max<std::size_t>(1, (record_bytes + kBlocks - 1) / kBlocks);
  std::vector<std::uint32_t> block_quotes(kBlocks + 1, 0);
  {
    std::vector<std::uint32_t> block_parity(kBlocks, 0);
    practice::parallelRanges(kBlocks, workers, [&](std::size_t begin, std::size_t end) {
      std::ifstream in(records, std::ios::binary);
      if (!in)
        throw std::runtime_error("cannot open CSV: " + records.string());
      std::vector<char> window(block_bytes + 1);
      for (std::size_t b = begin; b < end; ++b) {
        const auto start = b * block_bytes;
        const auto stop = std::min(record_bytes, start + block_bytes);
        in.seekg(static_cast<std::streamoff>(start));
        in.read(window.data(), static_cast<std::streamsize>(stop - start));
        std::uint32_t quotes = 0;
        const auto got = static_cast<std::size_t>(in.gcount());
        for (std::size_t i = 0; i < got; ++i)
          if (window[i] == '"')
            ++quotes;
        block_parity[b] = quotes & 1u;
      }
    });
    for (std::size_t b = 0; b < kBlocks; ++b)
      block_quotes[b + 1] = block_quotes[b] + block_parity[b];
  }
  std::vector<std::size_t> boundary(kShards + 1, 0);
  {
    std::ifstream in(records, std::ios::binary);
    if (!in)
      throw std::runtime_error("cannot open CSV: " + records.string());
    for (std::size_t s = 1; s < kShards; ++s) {
      const auto block = (record_bytes * s / kShards) / block_bytes;
      bool in_quotes = (block_quotes[block] & 1u) != 0;
      auto position = block * block_bytes;
      in.seekg(static_cast<std::streamoff>(position));
      std::size_t found = record_bytes;
      while (position < record_bytes) {
        const auto byte = in.get();
        if (byte == std::char_traits<char>::eof())
          break;
        ++position;
        if (in_quotes) {
          if (byte == '"')
            in_quotes = false;
        } else if (byte == '"') {
          in_quotes = true;
        } else if (byte == '\n') {
          found = position;
          break;
        }
      }
      boundary[s] = found;
    }
    boundary[kShards] = record_bytes;
  }

  struct Local {
    std::uint64_t count = 0;
    WideCents total;
  };
  const auto groups = aggregates.size();
  std::vector<std::vector<Local>> local(kShards, std::vector<Local>(groups));
  std::vector<std::exception_ptr> shard_errors(kShards);
  practice::parallelRanges(kShards, workers, [&](std::size_t sb, std::size_t se) {
    for (std::size_t s = sb; s < se; ++s) {
      try {
        CsvReader csv(records, boundary[s], boundary[s + 1]);
        std::vector<std::string> shard_row;
        if (s == 0) {
          if (!csv.next(shard_row) ||
              shard_row != std::vector<std::string>{"id", "name", "note", "region_id", "amount"})
            throw std::runtime_error("invalid practice records header");
        }
        auto& mine = local[s];
        while (csv.next(shard_row, true)) {
          if (shard_row.size() != 5)
            throw std::runtime_error("record must contain five columns");
          integer<std::uint64_t>(shard_row[0]);
          const auto region = integer<std::uint32_t>(shard_row[3]);
          const auto amount = parseCents(shard_row[4]);
          Region* matched_region = nullptr;
          if (!dense_regions.empty()) {
            if (region >= min_region_id &&
                static_cast<std::uint64_t>(region) - min_region_id < dense_regions.size())
              matched_region = dense_regions[region - min_region_id];
          } else {
            const auto found = regions.find(region);
            if (found != regions.end())
              matched_region = &found->second;
          }
          if (!matched_region)
            continue;
          auto& entry = mine[matched_region->group];
          if (entry.count == std::numeric_limits<std::uint64_t>::max())
            throw std::runtime_error("aggregate count overflow");
          ++entry.count;
          entry.total.add(amount);
        }
      } catch (...) {
        shard_errors[s] = std::current_exception();
      }
    }
  });
  for (const auto& error : shard_errors)
    if (error)
      std::rethrow_exception(error);
  for (std::size_t s = 0; s < kShards; ++s) {
    for (std::size_t g = 0; g < groups; ++g) {
      aggregates[g].count += local[s][g].count;
      const auto before = aggregates[g].total.low;
      aggregates[g].total.low += local[s][g].total.low;
      if (aggregates[g].total.low < before)
        ++aggregates[g].total.high;
      aggregates[g].total.high += local[s][g].total.high;
    }
  }
  std::ostringstream out;
  out << "region,count,amount\n";
  std::sort(aggregates.begin(), aggregates.end(),
            [](const Aggregate& a, const Aggregate& b) { return a.region < b.region; });
  for (const auto& aggregate : aggregates) {
    if (aggregate.count)
      out << formatCsvField(aggregate.region) << ',' << aggregate.count << ','
          << formatMoney(aggregate.total.checked()) << '\n';
  }
  writeFile(output, out.str());
}
}
