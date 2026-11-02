# Track D — Traverse

Exact zero-dependency Rust implementation of the official directed shortest-path
Track D specification.

The graph is memory-mapped and built into packed forward and reverse CSR arrays.
Parallel BFS locality renumbering groups nearby vertices before CSR scatter;
the remapping is bijective and preserves every directed edge and weight.
Heavy weights use an exact full-width side table. Parallel edges, zero weights,
disconnected components and duplicate queries are preserved.

Queries are sorted by remapped endpoints for cache locality, then run with
parallel exact bidirectional Dijkstra. Each direction uses a monotone radix
heap and sparse generation-stamped distance table with 64-bit distances.
Every relaxation uses the full exact edge weight. Expansion stops only when
the sum of the two valid frontier lower bounds meets the best observed path;
unreachable queries output -1.

The full public-seed checksum and multiple independently generated reference
seeds are used as correctness gates. A two-level wheel achieved **68.363711** points / 168.724 ms
(`run-f0a2ded6fcfd`) but also scored **67.732133** on a separate
official run. Its additional source length and score variability prevented
promotion; the selected source remains the reproducibly verified radix heap
(`6759b53`, 68.316671 points). Experimental results and rejected alternatives
are recorded with exact CI links in `PERF.md`.

Dependencies: none.

With another 24 hours: profile CSR construction versus frontier expansion and
test exact preprocessing only when its complete build+query cost improves K.
