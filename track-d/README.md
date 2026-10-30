# Track D: Traverse

The task, the input files and the exact output format are in the [spec](https://github.com/hackathon-raptors/speed-demon-2026/blob/main/specs/d-traverse.md).

## Approach

Two parallel block-buffered passes with newline scanning build forward and reverse compressed sparse-row
graphs using atomic degree counts and fill cursors. Six-byte edges store a
uint32 destination and uint16 weight; a sentinel routes weights of 65535 or more
to exact uint32 overflow maps. Directedness, parallel edges and zero weights are
preserved.

Twelve workers independently answer queries with bidirectional Dijkstra,
64-bit monotone radix queues and sparse distance tables reused by generation
stamp. Search balances queue sizes rather than distances to avoid pathological
imbalance from large weights. The exact minimum-frontier bound terminates a
query; unreachable pairs produce -1. Distances above uint32 remain exact.

## Dependencies

None.

## With another 24 hours

Profile graph construction and queue expansion on the scoring CPU. A dense
distance-array experiment was rejected because it increased runtime and memory.
Current local full seed-1 median: 0.356099 s, 135.66 MiB peak RSS, trading more
memory than the old baseline for faster search. No official run ID is available
for this candidate; see [PERF.md](PERF.md).
