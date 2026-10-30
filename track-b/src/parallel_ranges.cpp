#include "parallel_ranges.hpp"

#include <algorithm>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace practice {

void parallelRanges(std::size_t count, std::size_t workers, const RangeWork& work) {
  if (count == 0)
    return;
  if (!work)
    throw std::invalid_argument("parallelRanges requires a work function");
  workers = std::max<std::size_t>(1, std::min({workers, count, maxWorkers}));
  std::exception_ptr failure;
  std::mutex failure_mutex;
  const auto runRange = [&](std::size_t begin, std::size_t end) {
    try {
      work(begin, end);
    } catch (...) {
      std::lock_guard lock(failure_mutex);
      if (!failure)
        failure = std::current_exception();
    }
  };
  if (workers == 1) {
    runRange(0, count);
    if (failure)
      std::rethrow_exception(failure);
    return;
  }

  const auto chunk_size = count / workers + (count % workers != 0);
  std::vector<std::jthread> threads;
  threads.reserve(workers - 1);
  for (std::size_t worker = 1; worker < workers; ++worker) {
    const auto begin = worker * chunk_size;
    const auto end = std::min(count, begin + chunk_size);
    if (begin >= end)
      break;
    threads.emplace_back([=, &runRange] { runRange(begin, end); });
  }
  runRange(0, std::min(count, chunk_size));
  for (auto& thread : threads)
    thread.join();
  if (failure)
    std::rethrow_exception(failure);
}

}
