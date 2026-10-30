#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "track_a.hpp"

namespace lab {
namespace {

using u32 = std::uint32_t;
using u64 = std::uint64_t;

constexpr std::size_t kWin = 1 << 19;
constexpr std::size_t kHours = 128;
constexpr std::size_t kSlots = 1 << 15;
constexpr std::size_t kEids = kSlots / 4;
constexpr std::size_t kAll = (kEids + 1) * kHours;
constexpr std::size_t kText = 1 << 21;
constexpr unsigned kGroupIdBits = 20;
constexpr unsigned kMetricBits = 22;
constexpr u64 kMetricMask = (u64{1} << kMetricBits) - 1;
constexpr u64 kMix = 0x9E3779B97F4A7C15ull;
static_assert(kEids * kHours <= (std::size_t{1} << kGroupIdBits));
static_assert(kGroupIdBits + 2 * kMetricBits == 64);

struct Slot {
  std::atomic<u64> tag{0};
  u32 off = 0, len = 0, id = 0;
};
struct Hour {
  std::atomic<u64> a{0}, b{0};
  u32 hid = 0;
};
struct ParsedRow {
  u32 gid, lat, err;
};

class CapacityExceeded : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

u64 load64(const char* p) {
  u64 v;
  std::memcpy(&v, p, 8);
  return v;
}

u64 hashOf(const char* p, std::size_t n) {
  u64 h = kMix ^ n;
  std::size_t i = 0;
  for (; i + 8 <= n; i += 8)
    h = (h ^ load64(p + i)) * kMix;
  if (i < n) {
    u64 w = 0;
    std::memcpy(&w, i ? p + n - 8 : p, n < 8 ? n : 8);
    h = (h ^ w) * kMix;
  }
  return h | 1;
}

struct Ctx {
  std::vector<Slot> slots = std::vector<Slot>(kSlots);
  std::vector<char> text = std::vector<char>(kText);
  std::vector<u32> epOff = std::vector<u32>(kEids), epLen = std::vector<u32>(kEids);
  std::array<Hour, kHours> hours{};
  std::vector<std::string> hourText;
  std::mutex mu;

  u32 ep(const char* p, u32 n) {
    const u64 h = hashOf(p, n);
    for (std::size_t probes = 0, i = h & (kSlots - 1); probes < kSlots;
         ++probes, i = (i + 1) & (kSlots - 1)) {
      const Slot& s = slots[i];
      if (s.tag.load(std::memory_order_acquire) == h && s.len == n &&
          std::memcmp(text.data() + s.off, p, n) == 0)
        return s.id;
      if (!s.tag.load(std::memory_order_relaxed))
        return epAdd(p, n, h);
    }
    throw CapacityExceeded("endpoint table is full");
  }

  u32 epAdd(const char* p, u32 n, u64 h) {
    std::lock_guard<std::mutex> lock(mu);
    for (std::size_t probes = 0, i = h & (kSlots - 1); probes < kSlots;
         ++probes, i = (i + 1) & (kSlots - 1)) {
      Slot& s = slots[i];
      const u64 t = s.tag.load(std::memory_order_relaxed);
      if (t == h && s.len == n && std::memcmp(text.data() + s.off, p, n) == 0)
        return s.id;
      if (!t)
        return publish(s, p, n, h);
    }
    throw CapacityExceeded("endpoint table is full");
  }

  u32 publish(Slot& s, const char* p, u32 n, u64 h) {
    if (eids >= kEids || used + n > kText)
      throw CapacityExceeded("endpoint table capacity exceeded");
    const u32 id = eids++;
    std::memcpy(text.data() + used, p, n);
    epOff[id] = static_cast<u32>(used);
    epLen[id] = n;
    s.off = epOff[id];
    s.len = n;
    s.id = id;
    used += n;
    s.tag.store(h, std::memory_order_release);
    return id;
  }

  u32 eids = 0;
  std::size_t used = 0;

  u32 hour(const char* p) {
    const u64 a = load64(p), b = load64(p + 5);
    for (std::size_t probes = 0, i = ((a ^ (b * kMix)) >> 47) & (kHours - 1); probes < kHours;
         ++probes, i = (i + 1) & (kHours - 1)) {
      Hour& h = hours[i];
      const u64 ha = h.a.load(std::memory_order_acquire);
      if (ha == a && h.b.load(std::memory_order_relaxed) == b)
        return h.hid;
      if (!ha)
        return hourAdd(p, a, b, i);
    }
    throw CapacityExceeded("UTC hour table capacity exceeded");
  }

  u32 hourAdd(const char* p, u64 a, u64 b, std::size_t i) {
    std::lock_guard<std::mutex> lock(mu);
    for (std::size_t probes = 0; probes < kHours; ++probes, i = (i + 1) & (kHours - 1)) {
      Hour& h = hours[i];
      const u64 ha = h.a.load(std::memory_order_relaxed);
      if (ha == a && h.b.load(std::memory_order_relaxed) == b)
        return h.hid;
      if (ha)
        continue;
      if (p[4] != '-' || p[7] != '-' || p[10] != 'T' || p[13] != ':' || p[16] != ':' ||
          p[19] != '.' || p[26] != 'Z')
        throw std::runtime_error("practice timestamp must be UTC with six fractional digits");
      const unsigned year = integer<unsigned>(std::string_view(p, 4));
      const unsigned month = integer<unsigned>(std::string_view(p + 5, 2));
      const unsigned day = integer<unsigned>(std::string_view(p + 8, 2));
      const unsigned clock = integer<unsigned>(std::string_view(p + 11, 2));
      if (integer<unsigned>(std::string_view(p + 14, 2)) > 59 ||
          integer<unsigned>(std::string_view(p + 17, 2)) > 59)
        throw std::runtime_error("timestamp outside calendar range");
      constexpr std::array<unsigned, 12> days{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
      const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
      if (year == 0 || month < 1 || month > 12 || day < 1 || clock > 23 ||
          day > days[month - 1] + (month == 2 && leap ? 1U : 0U))
        throw std::runtime_error("timestamp outside calendar range");
      if (hourText.size() >= kHours)
        throw CapacityExceeded("UTC hour table capacity exceeded");
      h.hid = static_cast<u32>(hourText.size());
      h.b.store(b, std::memory_order_relaxed);
      h.a.store(a, std::memory_order_release);
      hourText.emplace_back(p, 13);
      return h.hid;
    }
    throw CapacityExceeded("UTC hour table capacity exceeded");
  }
};

ParsedRow parseRow(Ctx& c, const char* p, const char* e) {
  if (e - p < 39)
    throw std::runtime_error("log line too short");
  if (e[-1] == '\r')
    --e;
  if (p[27] != ' ')
    throw std::runtime_error("log must contain five fields");
  if (p[4] != '-' || p[7] != '-' || p[10] != 'T' || p[13] != ':' || p[16] != ':' || p[19] != '.' ||
      p[26] != 'Z')
    throw std::runtime_error("practice timestamp must be UTC with six fractional digits");
  const unsigned mm = static_cast<unsigned>(p[14] - '0') * 10 + static_cast<unsigned>(p[15] - '0');
  const unsigned ss = static_cast<unsigned>(p[17] - '0') * 10 + static_cast<unsigned>(p[18] - '0');
  if (p[14] < '0' || p[14] > '9' || p[15] < '0' || p[15] > '9' || p[17] < '0' || p[17] > '9' ||
      p[18] < '0' || p[18] > '9' || mm > 59 || ss > 59)
    throw std::runtime_error("timestamp outside calendar range");
  for (const char* f = p + 20; f < p + 26; ++f)
    if (*f < '0' || *f > '9')
      throw std::runtime_error("invalid timestamp digit");
  const u32 hid = c.hour(p);
  const char* q = p + 28;
  while (q < e && *q != ' ')
    ++q;
  if (q >= e)
    throw std::runtime_error("log must contain five fields");
  const std::string_view method(p + 28, static_cast<std::size_t>(q - (p + 28)));
  constexpr std::array<std::string_view, 7> methods{"GET",    "POST", "PUT",    "PATCH",
                                                    "DELETE", "HEAD", "OPTIONS"};
  if (std::find(methods.begin(), methods.end(), method) == methods.end())
    throw std::runtime_error("unknown HTTP method");
  const char* m = q + 1;
  const char* z = e;
  while (z > m && z[-1] >= '0' && z[-1] <= '9')
    --z;
  const auto latency_digits = e - z;
  if (latency_digits == 0 || latency_digits > 10)
    throw std::runtime_error("latency outside uint32 range");
  u64 latency = 0;
  for (const char* d = z; d < e; ++d)
    latency = latency * 10 + static_cast<u64>(*d - '0');
  if (latency > 0xFFFFFFFFull)
    throw std::runtime_error("latency outside uint32 range");
  if (z - 5 <= m || z[-1] != ' ' || z[-5] != ' ')
    throw std::runtime_error("log must contain five fields");
  for (const char* s = z - 4; s < z - 1; ++s)
    if (*s < '0' || *s > '9')
      throw std::runtime_error("invalid HTTP status");
  const u32 status = static_cast<u32>(z[-4] - '0') * 100 + static_cast<u32>(z[-3] - '0') * 10 +
                     static_cast<u32>(z[-2] - '0');
  if (status < 100 || status > 599)
    throw std::runtime_error("invalid HTTP status");
  const auto endpoint_size = static_cast<std::size_t>(z - 5 - m);
  if (endpoint_size > std::numeric_limits<u32>::max())
    throw CapacityExceeded("endpoint length exceeds fast-path capacity");
  const u32 eid = c.ep(m, static_cast<u32>(endpoint_size));
  return {eid * static_cast<u32>(kHours) + hid, static_cast<u32>(latency), status >= 500 ? 1U : 0U};
}

template <class F>
void scanLines(const std::filesystem::path& path, std::size_t begin, std::size_t end, F&& fn) {
  std::ifstream in(path, std::ios::binary);
  if (!in)
    throw std::runtime_error("cannot open logs: " + path.string());
  std::vector<char> buf(kWin);
  std::size_t carry = 0;
  while (begin < end) {
    const std::size_t want = std::min(buf.size() - carry, end - begin);
    in.seekg(static_cast<std::streamoff>(begin));
    in.read(buf.data() + carry, static_cast<std::streamsize>(want));
    const std::size_t got = static_cast<std::size_t>(in.gcount());
    if (!got)
      throw std::runtime_error("failed reading logs: " + path.string());
    begin += got;
    const std::size_t total = carry + got;
    std::size_t stop = total;
    if (begin < end)
      while (stop && buf[stop - 1] != '\n')
        --stop;
    if (!stop) {
      buf.resize(total * 2);
      carry = total;
      continue;
    }
    const char* p = buf.data();
    const char* lim = p + stop;
    while (p < lim) {
      const char* nl =
          static_cast<const char*>(std::memchr(p, '\n', static_cast<std::size_t>(lim - p)));
      const char* nx = nl ? nl : lim;
      fn(p, nx);
      p = nx + 1;
    }
    carry = total - stop;
    std::memmove(buf.data(), buf.data() + stop, carry);
  }
}

void appendNumber(std::string& out, std::uint64_t value) {
  char buf[24];
  const auto end = std::to_chars(buf, buf + sizeof buf, value).ptr;
  out.append(buf, end);
}

std::string_view validatedHour(std::string_view timestamp, std::string& validated_date) {
  if (timestamp.size() != 27 || timestamp[4] != '-' || timestamp[7] != '-' ||
      timestamp[10] != 'T' || timestamp[13] != ':' || timestamp[16] != ':' ||
      timestamp[19] != '.' || timestamp[26] != 'Z')
    throw std::runtime_error("practice timestamp must be UTC with six fractional digits");
  for (std::size_t i = 0; i < timestamp.size(); ++i) {
    if (i == 4 || i == 7 || i == 10 || i == 13 || i == 16 || i == 19 || i == 26)
      continue;
    if (timestamp[i] < '0' || timestamp[i] > '9')
      throw std::runtime_error("invalid timestamp digit");
  }
  const auto date = timestamp.substr(0, 10);
  if (date != validated_date) {
    const auto year = integer<unsigned>(timestamp.substr(0, 4));
    const auto month = integer<unsigned>(timestamp.substr(5, 2));
    const auto day = integer<unsigned>(timestamp.substr(8, 2));
    constexpr std::array<unsigned, 12> days{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    if (year == 0 || month < 1 || month > 12 || day < 1 ||
        day > days[month - 1] + (month == 2 && leap ? 1U : 0U))
      throw std::runtime_error("timestamp outside calendar range");
    validated_date = date;
  }
  if (integer<unsigned>(timestamp.substr(11, 2)) > 23 ||
      integer<unsigned>(timestamp.substr(14, 2)) > 59 ||
      integer<unsigned>(timestamp.substr(17, 2)) > 59)
    throw std::runtime_error("timestamp outside calendar range");
  return timestamp.substr(0, 13);
}

struct SlowGroup {
  std::string endpoint, hour;
  std::size_t begin = 0, end = 0;
  std::uint64_t count = 0, errors = 0;
};
struct SlowGroupKey {
  std::string_view endpoint, hour;
  friend bool operator==(const SlowGroupKey&, const SlowGroupKey&) = default;
};
struct SlowGroupKeyHash {
  std::size_t operator()(const SlowGroupKey& key) const noexcept {
    const auto endpoint = std::hash<std::string_view>{}(key.endpoint);
    const auto hour = std::hash<std::string_view>{}(key.hour);
    return endpoint ^ (hour + std::size_t{0x9e3779b9} + (endpoint << 6) + (endpoint >> 2));
  }
};
struct SlowRecord {
  std::size_t group;
  u32 latency;
};

void ingestDynamic(const std::filesystem::path& input, const std::filesystem::path& output) {
  std::ifstream in(input, std::ios::binary);
  if (!in)
    throw std::runtime_error("cannot open logs: " + input.string());
  std::unordered_map<SlowGroupKey, std::size_t, SlowGroupKeyHash> group_ids;
  std::deque<SlowGroup> groups;
  std::vector<SlowRecord> records;
  std::string line, validated_date;
  constexpr std::array<std::string_view, 7> methods{"GET",    "POST", "PUT",    "PATCH",
                                                    "DELETE", "HEAD", "OPTIONS"};
  while (std::getline(in, line)) {
    std::array<std::string_view, 5> parts;
    std::string_view remaining = line;
    for (auto& part : parts) {
      const auto start = remaining.find_first_not_of(" \t\r");
      if (start == remaining.npos)
        throw std::runtime_error("log must contain five fields");
      remaining.remove_prefix(start);
      const auto end = remaining.find_first_of(" \t\r");
      part = remaining.substr(0, end);
      remaining.remove_prefix(end == remaining.npos ? remaining.size() : end);
    }
    if (remaining.find_first_not_of(" \t\r") != remaining.npos)
      throw std::runtime_error("log must contain five fields");
    const auto bucket = validatedHour(parts[0], validated_date);
    if (std::find(methods.begin(), methods.end(), parts[1]) == methods.end())
      throw std::runtime_error("unknown HTTP method");
    const auto status = integer<std::uint16_t>(parts[3]);
    if (status < 100 || status > 599)
      throw std::runtime_error("invalid HTTP status");
    const auto latency = integer<u32>(parts[4]);
    const SlowGroupKey key{parts[2], bucket};
    auto found = group_ids.find(key);
    std::size_t group_id;
    if (found == group_ids.end()) {
      group_id = groups.size();
      groups.push_back({std::string(parts[2]), std::string(bucket)});
      const auto& group = groups.back();
      group_ids.emplace(SlowGroupKey{group.endpoint, group.hour}, group_id);
    } else {
      group_id = found->second;
    }
    auto& group = groups[group_id];
    ++group.count;
    group.errors += status >= 500;
    records.push_back({group_id, latency});
  }
  if (in.bad())
    throw std::runtime_error("failed reading logs: " + input.string());

  std::vector<std::size_t> cursor(groups.size());
  std::size_t total = 0;
  for (std::size_t i = 0; i < groups.size(); ++i) {
    groups[i].begin = total;
    if (groups[i].count > std::numeric_limits<std::size_t>::max() - total)
      throw std::runtime_error("too many log records");
    total += static_cast<std::size_t>(groups[i].count);
    groups[i].end = total;
    cursor[i] = groups[i].begin;
  }
  std::vector<u32> latencies(total);
  for (const auto& record : records)
    latencies[cursor[record.group]++] = record.latency;
  std::sort(groups.begin(), groups.end(), [](const SlowGroup& a, const SlowGroup& b) {
    return std::tie(a.endpoint, a.hour) < std::tie(b.endpoint, b.hour);
  });

  std::string result = "endpoint\thour\tcount\terrors\terror_rate_ppm\tp50\tp95\tp99\n";
  for (const auto& group : groups) {
    auto* values = latencies.data() + group.begin;
    const auto count = group.end - group.begin;
    std::sort(values, values + count);
    result += group.endpoint;
    result.push_back('\t');
    result += group.hour;
    result += ":00:00Z\t";
    appendNumber(result, group.count);
    result.push_back('\t');
    appendNumber(result, group.errors);
    result.push_back('\t');
    appendNumber(result, (group.errors * 1'000'000 + group.count / 2) / group.count);
    for (const unsigned percent : {50U, 95U, 99U}) {
      result.push_back('\t');
      const auto rank = (count * percent + 99) / 100 - 1;
      appendNumber(result, values[rank]);
    }
    result.push_back('\n');
  }
  writeFile(output, result);
}
}

void ingest(const std::filesystem::path& input, const std::filesystem::path& output,
            std::size_t workers, std::string_view algorithm) {
  if (algorithm.empty())
    algorithm = "radix";
  if (algorithm != "radix")
    throw std::runtime_error("A algorithm must be radix");

  std::ifstream in(input, std::ios::binary | std::ios::ate);
  if (!in)
    throw std::runtime_error("cannot open logs: " + input.string());
  const auto sized = in.tellg();
  if (sized < 0)
    throw std::runtime_error("cannot size logs: " + input.string());
  const std::size_t size = static_cast<std::size_t>(sized);

  workers = std::clamp<std::size_t>(workers, 1, practice::maxWorkers);
  std::vector<std::size_t> bound(workers + 1);
  bound[workers] = size;
  {
    std::array<char, 1 << 16> probe{};
    for (std::size_t w = 1; w < workers; ++w) {
      std::size_t pos = size * w / workers;
      for (;;) {
        in.clear();
        in.seekg(static_cast<std::streamoff>(pos));
        in.read(probe.data(), static_cast<std::streamsize>(probe.size()));
        const std::size_t got = static_cast<std::size_t>(in.gcount());
        if (!got) {
          pos = size;
          break;
        }
        const char* nl = static_cast<const char*>(std::memchr(probe.data(), '\n', got));
        if (nl) {
          pos += static_cast<std::size_t>(nl - probe.data()) + 1;
          break;
        }
        pos += got;
      }
      bound[w] = pos;
    }
  }

  Ctx c;
  auto agg = std::make_unique<std::atomic<u64>[]>(kAll);
  try {
    practice::parallelRanges(workers, workers, [&](std::size_t wb, std::size_t we) {
      for (std::size_t w = wb; w < we; ++w)
        scanLines(input, bound[w], bound[w + 1], [&](const char* p, const char* e) {
          const ParsedRow row = parseRow(c, p, e);
          agg[row.gid].fetch_add(1 + (static_cast<u64>(row.err) << 32), std::memory_order_relaxed);
        });
    });
  } catch (const CapacityExceeded&) {
    agg.reset();
    ingestDynamic(input, output);
    return;
  }

  std::vector<u64> used;
  std::size_t total = 0;
  for (std::size_t g = 0; g < kAll; ++g) {
    const u64 v = agg[g].load(std::memory_order_relaxed);
    const u32 count = static_cast<u32>(v);
    const u32 errors = static_cast<u32>(v >> 32);
    if (!count)
      continue;
    if (count > kMetricMask || errors > kMetricMask ||
        count > std::numeric_limits<u32>::max() - total) {
      agg.reset();
      ingestDynamic(input, output);
      return;
    }
    used.push_back((static_cast<u64>(g) << (2 * kMetricBits)) |
                   (static_cast<u64>(count) << kMetricBits) | errors);
    agg[g].store(static_cast<u32>(total), std::memory_order_relaxed);
    total += count;
  }

  std::vector<u32> latency(total);
  practice::parallelRanges(workers, workers, [&](std::size_t wb, std::size_t we) {
    for (std::size_t w = wb; w < we; ++w)
      scanLines(input, bound[w], bound[w + 1], [&](const char* p, const char* e) {
        const ParsedRow row = parseRow(c, p, e);
        latency[agg[row.gid].fetch_add(1, std::memory_order_relaxed)] = row.lat;
      });
  });

  std::sort(used.begin(), used.end(), [&](u64 left, u64 right) {
    const u32 x = static_cast<u32>(left >> (2 * kMetricBits));
    const u32 y = static_cast<u32>(right >> (2 * kMetricBits));
    const std::string_view a(c.text.data() + c.epOff[x / kHours], c.epLen[x / kHours]);
    const std::string_view b(c.text.data() + c.epOff[y / kHours], c.epLen[y / kHours]);
    if (a != b)
      return a < b;
    return c.hourText[x % kHours] < c.hourText[y % kHours];
  });

  const std::size_t chunk = std::max<std::size_t>(1, (used.size() + workers - 1) / workers);
  std::vector<std::string> parts((used.size() + chunk - 1) / chunk);
  practice::parallelRanges(used.size(), workers, [&](std::size_t b, std::size_t e) {
    std::string& out = parts[b / chunk];
    for (std::size_t i = b; i < e; ++i) {
      const u64 group = used[i];
      const u32 gid = static_cast<u32>(group >> (2 * kMetricBits));
      const std::size_t count = static_cast<std::size_t>((group >> kMetricBits) & kMetricMask);
      const std::uint64_t errors = group & kMetricMask;
      u32* values = latency.data() + (agg[gid].load(std::memory_order_relaxed) - count);
      std::array<u32, 3> quantiles{};
      u32* begin = values;
      u32* end = values + count;
      std::size_t k = 0;
      for (const unsigned percent : {50U, 95U, 99U}) {
        const std::size_t rank = (count * percent + 99) / 100 - 1;
        std::nth_element(begin, values + rank, end);
        quantiles[k++] = values[rank];
        begin = values + rank;
      }
      out.append(c.text.data() + c.epOff[gid / kHours], c.epLen[gid / kHours]);
      out.push_back('\t');
      out.append(c.hourText[gid % kHours]);
      out += ":00:00Z\t";
      appendNumber(out, count);
      out.push_back('\t');
      appendNumber(out, errors);
      out.push_back('\t');
      appendNumber(out, (errors * 1000000 + count / 2) / count);
      for (const u32 value : quantiles) {
        out.push_back('\t');
        appendNumber(out, value);
      }
      out.push_back('\n');
    }
  });

  std::ofstream os(output, std::ios::binary);
  if (!os)
    throw std::runtime_error("cannot open output: " + output.string());
  os << "endpoint\thour\tcount\terrors\terror_rate_ppm\tp50\tp95\tp99\n";
  for (const auto& part : parts)
    os.write(part.data(), static_cast<std::streamsize>(part.size()));
  os.close();
  if (!os)
    throw std::runtime_error("failed writing output: " + output.string());
}
}
