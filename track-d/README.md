# Track D — Traverse

The Rust executable builds a compact weighted directed graph with parallel CSR construction, exact side storage for large weights and BFS-based locality renumbering. Bidirectional Dijkstra queries run in parallel with monotone radix queues and sparse distance state. The algorithm preserves zero-weight edges, parallel edges, disconnected components and 64-bit path lengths.

- Language: Rust, no external runtime dependencies
- Inputs: `/data/graph.txt`, `/data/queries.txt`
- Output: `/out/distances.txt`
- Correctness: exact path relaxation and only proven lower-bound stopping conditions

This is the earlier radix implementation, rather than a later unverified queue experiment. Its measured history is in `PERF.md`.
