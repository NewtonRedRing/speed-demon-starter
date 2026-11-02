#pragma once
#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace hpc {
inline std::filesystem::path input_dir = "data";
inline std::filesystem::path output_dir = "out";
inline unsigned thread_limit = 0;

inline unsigned parse_threads(const char* value) {
    if (!value || !*value || *value == '-') throw std::invalid_argument("threads must be a positive integer");
    char* end = nullptr;
    errno = 0;
    const unsigned long parsed = std::strtoul(value, &end, 10);
    if (errno || !end || *end || parsed == 0 || parsed > 256)
        throw std::invalid_argument("threads must be between 1 and 256");
    return static_cast<unsigned>(parsed);
}
inline void configure(int argc, char** argv) {
    if (const char* p = std::getenv("HPC_INPUT_DIR")) input_dir = p;
    if (const char* p = std::getenv("HPC_OUTPUT_DIR")) output_dir = p;
    if (const char* p = std::getenv("HPC_THREADS")) thread_limit = parse_threads(p);
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--input-dir" && i + 1 < argc) input_dir = argv[++i];
        else if (arg == "--output-dir" && i + 1 < argc) output_dir = argv[++i];
        else if (arg == "--threads" && i + 1 < argc) thread_limit = parse_threads(argv[++i]);
        else throw std::invalid_argument("usage: <executable> [--input-dir DIR] [--output-dir DIR] [--threads N]");
    }
    if (input_dir.empty() || output_dir.empty()) throw std::invalid_argument("input/output directory cannot be empty");
    std::filesystem::create_directories(output_dir);
}
inline std::string input_path(std::string_view filename) {
    return (input_dir / filename).string();
}
inline std::string output_path(std::string_view filename) {
    return (output_dir / filename).string();
}
inline unsigned worker_count(unsigned recommended) {
    if (!recommended) return 1;
    return std::max(1u, std::min(recommended, thread_limit ? thread_limit : recommended));
}
} // namespace hpc
