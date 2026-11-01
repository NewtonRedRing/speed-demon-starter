# Track D — Traverse

Exact zero-dependency Rust implementation of the official directed shortest-path
Track D specification.

The graph is memory-mapped and built into packed forward and reverse CSR arrays,
then ordered into BFS locality blobs of 512 nodes.
Weights that do not fit the packed field use an exact full-width side table;
parallel edges, zero weights and directedness are preserved.

Queries run in parallel with exact bidirectional Dijkstra. Each direction uses
a monotone radix heap and sparse paged distance table, with 64-bit
distances throughout. The search stops only at the valid minimum-frontier bound;
unreachable queries produce -1.

Dependencies: none.

With another 24 hours: profile CSR construction versus frontier expansion and
test exact preprocessing only when its complete build+query cost improves K.
