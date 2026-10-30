#include <algorithm>
#include <cstdint>
#include <deque>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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

struct Aggregate {
    std::uint64_t count = 0;
    std::uint64_t id_sum = 0;
    std::int64_t amount_sum = 0;
    std::int64_t amount_min = std::numeric_limits<std::int64_t>::max();
    std::int64_t amount_max = std::numeric_limits<std::int64_t>::min();
    std::uint64_t name_bytes = 0;
    std::uint64_t note_bytes = 0;
};

std::uint64_t parse_unsigned(std::string_view text) {
    std::uint64_t value = 0;
    for (char c : text) value = value * 10 + static_cast<unsigned>(c - '0');
    return value;
}

std::int64_t parse_cents(std::string_view text) {
    bool negative = !text.empty() && text.front() == '-';
    if (negative) text.remove_prefix(1);
    const std::size_t dot = text.find('.');
    const std::uint64_t whole = parse_unsigned(text.substr(0, dot));
    const std::uint64_t fraction = parse_unsigned(text.substr(dot + 1));
    const std::int64_t cents = static_cast<std::int64_t>(whole * 100 + fraction);
    return negative ? -cents : cents;
}

std::string format_cents(std::int64_t value) {
    const bool negative = value < 0;
    const std::uint64_t magnitude = negative ? static_cast<std::uint64_t>(-value)
                                             : static_cast<std::uint64_t>(value);
    std::string result = negative ? "-" : "";
    result += std::to_string(magnitude / 100);
    result += '.';
    if (magnitude % 100 < 10) result += '0';
    result += std::to_string(magnitude % 100);
    return result;
}

bool starts_bom(const std::string& data, std::size_t pos) {
    return pos + 3 <= data.size() &&
           static_cast<unsigned char>(data[pos]) == 0xef &&
           static_cast<unsigned char>(data[pos + 1]) == 0xbb &&
           static_cast<unsigned char>(data[pos + 2]) == 0xbf;
}

std::size_t parse_field(const std::string& data, std::size_t& pos, std::string* value) {
    if (value) value->clear();
    std::size_t decoded_length = 0;
    if (pos < data.size() && data[pos] == '"') {
        ++pos;
        while (pos < data.size()) {
            const char c = data[pos++];
            if (c == '"') {
                if (pos < data.size() && data[pos] == '"') {
                    if (value) value->push_back('"');
                    ++decoded_length;
                    ++pos;
                } else {
                    break;
                }
            } else {
                if (value) value->push_back(c);
                ++decoded_length;
            }
        }
    } else {
        while (pos < data.size() && data[pos] != ',' && data[pos] != '\r' && data[pos] != '\n') {
            if (value) value->push_back(data[pos]);
            ++decoded_length;
            ++pos;
        }
    }

    if (pos < data.size() && data[pos] == ',') {
        ++pos;
    } else if (pos < data.size() && data[pos] == '\r') {
        pos += pos + 1 < data.size() && data[pos + 1] == '\n' ? 2 : 1;
    } else if (pos < data.size() && data[pos] == '\n') {
        ++pos;
    }
    return decoded_length;
}

bool byte_less(const std::string& a, const std::string& b) {
    return std::lexicographical_compare(
        a.begin(), a.end(), b.begin(), b.end(),
        [](unsigned char x, unsigned char y) { return x < y; });
}

int main() {
    std::ios::sync_with_stdio(false);
    
    std::ifstream regions_file("/data/regions.csv", std::ios::binary);
    if (!regions_file) return 1;
    regions_file.rdbuf()->pubsetbuf(nullptr, 256 * 1024);
    std::vector<std::string> countries{"_UNMATCHED"};
    std::unordered_map<std::string, std::uint16_t, TransparentHash, TransparentEqual> country_ids;
    country_ids.reserve(256);
    std::vector<std::int16_t> region_country(1000000, -1);
    std::string line;
    if (!std::getline(regions_file, line) || line != "region_id,country") return 1;
    while (std::getline(regions_file, line)) {
        const std::size_t comma = line.find(',');
        if (comma == std::string::npos) return 1;
        const auto region = static_cast<std::uint32_t>(parse_unsigned(std::string_view(line).substr(0, comma)));
        const std::string_view code(line.data() + comma + 1, line.size() - comma - 1);
        auto it = country_ids.find(code);
        if (it == country_ids.end()) {
            const auto id = static_cast<std::uint16_t>(countries.size());
            countries.emplace_back(code);
            country_ids.emplace(countries.back(), id);
            it = country_ids.find(code);
        }
        if (region < region_country.size()) region_country[region] = static_cast<std::int16_t>(it->second);
    }
    if (regions_file.bad()) return 1;

    std::ifstream records_file("/data/records.csv", std::ios::binary | std::ios::ate);
    if (!records_file) return 1;
    const std::streamsize size = records_file.tellg();
    if (size < 0) return 1;
    std::string data(static_cast<std::size_t>(size), '\0');
    records_file.seekg(0);
    if (!records_file.read(data.data(), size)) return 1;

    std::size_t pos = 0;
    if (starts_bom(data, pos)) pos += 3;
    for (int field = 0; field < 5; ++field) parse_field(data, pos, nullptr);

    std::vector<Aggregate> aggregates(countries.size());
    // Unquoted names are referenced directly in the immutable input buffer.
    // Only decoded quoted names require separate, stable storage.
    using NameSet = std::unordered_set<std::string_view, TransparentHash, TransparentEqual>;
    std::vector<NameSet> names(countries.size());
    std::deque<std::string> quoted_name_storage;
    std::string value, name;
    while (pos < data.size()) {
        if (starts_bom(data, pos)) pos += 3;
        if (pos >= data.size()) break;
        parse_field(data, pos, &value);
        const std::uint64_t record_id = parse_unsigned(value);
        const std::size_t name_start = pos;
        const bool quoted_name = pos < data.size() && data[pos] == '"';
        const std::size_t name_length = parse_field(data, pos, quoted_name ? &name : nullptr);
        const std::string_view name_view = quoted_name
            ? std::string_view(name)
            : std::string_view(data.data() + name_start, name_length);
        const std::uint64_t note_length = parse_field(data, pos, nullptr);
        parse_field(data, pos, &value);
        const std::uint64_t region_id = parse_unsigned(value);
        parse_field(data, pos, &value);
        const std::int64_t amount = parse_cents(value);

        std::size_t country = 0;
        if (region_id < region_country.size() && region_country[region_id] >= 0)
            country = static_cast<std::size_t>(region_country[region_id]);
        Aggregate& aggregate = aggregates[country];
        ++aggregate.count;
        aggregate.id_sum += record_id;
        aggregate.amount_sum += amount;
        aggregate.amount_min = std::min(aggregate.amount_min, amount);
        aggregate.amount_max = std::max(aggregate.amount_max, amount);
        aggregate.name_bytes += name_length;
        aggregate.note_bytes += note_length;
        NameSet& set = names[country];
        if (set.find(name_view) == set.end()) {
            if (quoted_name) {
                quoted_name_storage.emplace_back(std::move(name));
                set.emplace(std::string_view(quoted_name_storage.back()));
            } else {
                set.emplace(name_view);
            }
        }
    }

    std::vector<std::size_t> order(countries.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return byte_less(countries[a], countries[b]);
    });

    std::ofstream output("/out/aggregates.txt", std::ios::binary);
    if (!output) return 1;
    output.rdbuf()->pubsetbuf(nullptr, 256 * 1024);
    for (std::size_t i : order) {
        const Aggregate& a = aggregates[i];
        if (a.count == 0) continue;
        output << countries[i] << ' ' << a.count << ' ' << a.id_sum << ' '
               << format_cents(a.amount_sum) << ' ' << format_cents(a.amount_min) << ' '
               << format_cents(a.amount_max) << ' ' << names[i].size() << ' '
               << a.name_bytes << ' ' << a.note_bytes << '\n';
    }
    return output ? 0 : 1;
}
