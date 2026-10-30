#pragma once

#include <cstddef>
#include <filesystem>
#include <string_view>

#include "lab_common.hpp"
#include "parallel_ranges.hpp"

namespace lab {

void ingest(const std::filesystem::path& input, const std::filesystem::path& output,
            std::size_t workers, std::string_view algorithm);

}
