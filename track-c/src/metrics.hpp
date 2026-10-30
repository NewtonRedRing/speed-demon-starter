#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <ostream>
#include <string_view>

namespace practice {

struct RunProfile {
  std::string_view operation;
  std::string_view algorithm;
  std::size_t worker_threads = 1;
  double wall_seconds = 0;
};

inline void writeJsonString(std::ostream& stream, std::string_view value) {
  constexpr char hex[] = "0123456789abcdef";
  stream.put('"');
  for (const unsigned char character : value) {
    switch (character) {
      case '"':
        stream << "\\\"";
        break;
      case '\\':
        stream << "\\\\";
        break;
      case '\b':
        stream << "\\b";
        break;
      case '\f':
        stream << "\\f";
        break;
      case '\n':
        stream << "\\n";
        break;
      case '\r':
        stream << "\\r";
        break;
      case '\t':
        stream << "\\t";
        break;
      default:
        if (character < 0x20) {
          stream << "\\u00" << hex[character >> 4] << hex[character & 0x0f];
        } else {
          stream.put(static_cast<char>(character));
        }
        break;
    }
  }
  stream.put('"');
}

inline double elapsedSeconds(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

inline void writeRunProfile(std::ostream& stream, std::string_view prefix,
                            const RunProfile& profile) {
  stream << std::setprecision(17) << prefix << ": {\"operation\":";
  writeJsonString(stream, profile.operation);
  stream << ",\"algorithm\":";
  writeJsonString(stream, profile.algorithm);
  stream << ",\"worker_threads\":" << profile.worker_threads
         << ",\"wall_seconds\":" << profile.wall_seconds << "}\n";
}

}
