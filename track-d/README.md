# Track D: Traverse

The task, the input files and the exact output format are in the [spec](https://github.com/hackathon-raptors/speed-demon-2026/blob/main/specs/d-traverse.md).

## Approach

The program builds a compressed sparse-row representation of the directed graph and runs Dijkstra from each distinct query source. It stops once all targets for that source are settled, using 64-bit distances for paths above the uint32 edge-weight range.

## Dependencies

None.

## With another 24 hours

Measure the board run, then profile query search and evaluate further preprocessing only if it preserves exact shortest paths.
