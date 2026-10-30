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
