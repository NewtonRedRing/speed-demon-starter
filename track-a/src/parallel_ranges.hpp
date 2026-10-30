#pragma once

#include <cstddef>
#include <functional>

namespace practice {

inline constexpr std::size_t defaultWorkers = 12;
inline constexpr std::size_t maxWorkers = 12;

using RangeWork = std::function<void(std::size_t begin, std::size_t end)>;

void parallelRanges(std::size_t count, std::size_t workers, const RangeWork& work);

}
