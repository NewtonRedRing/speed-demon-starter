# Track D: Traverse

The task, the input files and the exact output format are in the [spec](https://github.com/hackathon-raptors/speed-demon-2026/blob/main/specs/d-traverse.md).

## Approach

Two parallel block-buffered passes build forward and reverse compressed sparse-row
graphs using atomic degree counts and fill cursors. Six-byte edges store a
uint32 destination and uint16 weight; a sentinel routes weights of 65535 or more
to exact uint32 overflow maps. Directedness, parallel edges and zero weights are
preserved.
Each block locates its last complete line once; numeric parsing advances
directly through complete rows without a separate newline search per edge.

Twelve workers independently answer queries with bidirectional Dijkstra,
64-bit monotone radix queues and sparse distance tables reused by generation
stamp. Search balances queue sizes rather than distances to avoid pathological
imbalance from large weights. The exact minimum-frontier bound terminates a
query; unreachable pairs produce -1. Distances above uint32 remain exact.
Relaxations at or above the current best path are skipped before looking up
the opposite frontier. Nonnegative weights preserve radix-key monotonicity.

## Dependencies

None.

## With another 24 hours

Profile graph construction and queue expansion on the scoring CPU. A dense
distance-array experiment was rejected because it increased runtime and memory.
Latest local five-run full seed-1 median: 0.399363 s, 133.26 MiB peak RSS.
The latest same-sandbox alternating tuning measured a 5.1% time reduction;
shared-host load prevents comparison against older isolated timings.
No official run ID is available
for this candidate; see [PERF.md](PERF.md).
