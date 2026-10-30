#pragma once

#include <charconv>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace lab {

inline void writeFile(const std::filesystem::path& path, std::string_view data) {
  std::ofstream out(path, std::ios::binary);
  if (!out)
    throw std::runtime_error("cannot open output: " + path.string());
  out.write(data.data(), static_cast<std::streamsize>(data.size()));
  out.close();
  if (!out)
    throw std::runtime_error("failed writing output: " + path.string());
}

template <class T>
T integer(std::string_view text) {
  if (text.empty())
    throw std::runtime_error("empty integer");
  T value{};
  auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size())
    throw std::runtime_error("invalid integer: " + std::string(text));
  return value;
}

}
