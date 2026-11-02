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

## 2026-11-02 packed-CSR / bucket-Dijkstra candidate (unscored)

Adapted from MIT-licensed [danse-macabre Track D](https://github.com/keirsalterego/danse-macabre/tree/speed-demon/track-d), Copyright (c) 2026 Hackathon Raptors CIC and its contributors. Features 4-byte packed edges with exact heavy-weight overflow side map, parallel CSR construction with per-owner chunk ranges, prefetch-aware bucket-queue bidirectional Dijkstra and sparse distance maps. Upstream board reported 118.4 ms / 65.7 MB / 73.6 points. Those are **upstream**, not our results. Preserve NewtonRedHeat original at f9c0a75900b951ba5fcf8a4e4bd74a4b7a6266e6, including the verified BFS locality path; test public and independent reference seeds and full K before promotion.
