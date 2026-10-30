#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace practice {
using Vertex = std::uint32_t;
using Weight = std::uint32_t;
using Distance = std::uint64_t;
constexpr Distance kInfinity = std::numeric_limits<Distance>::max();

struct EdgeInput {
  Vertex from, to;
  Distance weight;
};
struct Arc {
  Vertex to;
  Weight weight;
};
struct Query {
  Vertex from, to;
};

struct Graph {
  std::uint32_t vertices = 0;
  std::vector<std::size_t> offset;
  std::vector<Arc> arcs;
  std::vector<std::size_t> reverse_offset;
  std::vector<Arc> reverse_arcs;
};

}
