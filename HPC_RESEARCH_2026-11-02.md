# Speed Demon: HPC research and verified promotion policy (2026-11-02)

Scope: NewtonRedHeat / `speed-demon`. The **official public-score outcome**,
not CI time alone, decides whether an experiment enters the submission branch.
This file contains research hypotheses and experimentally verified results,
not unsubstantiated contest speedup claims. Four agents may work in parallel;
their experimental changes stay on scoped branches until compared with the
live scoring board.

## Scored baseline and rollback

From official public board feed audited at 11:20:19 UTC on November 2,
[board audit](https://github.com/NewtonRedRing/speed-demon-starter/actions/runs/41436015800):

| Track | Verified best points | Source / board commit | Runtime |
|---|---:|---|---:|
| A (ingest) | **84.036874** | HARP source; board commit `9bb3f6a` | 22.229 ms |
| B (index) | **72.172141** | board commit `c9f733b` | 183.883 ms |
| C (join) | **73.965629** | FALCON-C source; board commit `c0053b4` | 37.104 ms |
| D (traverse) | **68.316671** | radix heap; board commit `6759b53` | 174.011 ms |

The attempted Track D two-level exact wheel looked 7.25% faster
in a local benchmark but scored **67.732133** at 177.210 ms and
77,762,560 B on the official board, while its 532 counted source lines
also exceeded the verified radix-heap implementation's 485.
Restored the exact-scored radix source in `635c41c`.
Do not bring the wheel back without an independently scored improvement.

## New exact-run Track D profiling

[Profile workflow](https://github.com/NewtonRedRing/speed-demon-starter/actions/runs/41436438091)
ran five complete official full-seed-1 jobs with output hash
`a23dbdf0a3e49cc8901310a9ee1dc6a9a569a85297b6da75c2a7002cb2ed277e`
verified each time. Approximate median phase times on a GitHub runner:

| Stage | Median ms |
|---|---:|
| file map, parse, partitions | 52 |
| original CSR construction | 29 |
| BFS locality blobs | 57 |
| remapped forward CSR | 44 |
| reverse CSR | 37 |
| 2,000 shortest-path queries | **356** |
| full process | **590** |

This is **not the scoring CPU**. The exact relative bottleneck may change,
but query execution is ~60% of process time on the runner, with construction
and locality accounting for most remaining runtime.

[Identity-vs-locality workflow](https://github.com/NewtonRedRing/speed-demon-starter/actions/runs/41436785189)
verified the full official output for all 24 executions. A no-BFS
identity-vertex-order variant reached median **2072.084 ms**, compared
with **717.458 ms** for the existing locality-ordered graph:
**2.89x slower**. Consequently do not remove locality renumbering merely
to save construction time. The CPU cache benefit strongly outweighs
its setup cost in this experiment.

## Cutting-edge research targets (ranked by evidence and safe upside)

### D: exact, workload-aware bidirectional shortest paths

**Top candidate:** minimize node expansions / queue operations while
preserving exact Dijkstra stopping conditions. The input has 1,740,512
nodes, 5 million directed edges and 2,000 queries; graph IDs are random
and require locality restoration. The query distribution is logarithmic
in Dijkstra rank, so most queries are far smaller than the whole graph.

Test alternative *frontier selection* policies: smaller queue (current),
smaller minimum key, and measured queue-size biases. Order of choosing
which of the two valid Dijkstra frontiers to expand can change
performance without changing graph correctness. Check all exceptional
zero-weight, duplicate, disconnected, and >2^32 path cases with the
exact full digest and generated seeds. The code must not use
approximate heuristics or drop candidates.

**Frontier-policy check, completed:** [workflow run 41437023336](https://github.com/NewtonRedRing/speed-demon-starter/actions/runs/41437023336) compared four exact Rust search directions on the official full public D input, with nine randomized interleaved executions per variant and the official output SHA verified on every run:

| Policy | Median GitHub Actions elapsed | Baseline ratio |
|---|---:|---:|
| smaller queue (existing) | 718.857 ms | 1.0000 |
| smaller minimum distance | 1220.763 ms | 1.6982 |
| bias toward reverse frontier | 719.257 ms | 1.0006 |
| bias toward forward frontier | 717.016 ms | 0.9974 |

The minimum-distance rule is **strongly rejected**, since it expanded a much larger search frontier on this workload. Modest queue-size bias is indistinguishable from noise and adds instructions, so **do not promote** it. Exact source remains unmodified on the contest branch.

Second: fuse CSR scatter and renumbering while *retaining the same
BFS locality mapping*. The current builder constructs multiple complete
CSR versions. Any reduction in temporary graph lifetime or memory
may improve K, but must preserve output and source-size rules.

Third: adapt the sparse distance-page length (currently 512) and queue
layout as a combined measured whole-run optimization; already-isolated
wheel and page experiments should be used as negative controls.

2025 supercomputing work SPoCH describes parallel contraction hierarchy
preprocessing (https://doi.org/10.1145/3721145.3725744). Its benchmarks
do not predict speedups for a 2,000-query, ~0.2-second contest run;
construction must pay for itself. Avoid importing a banned shortest-path
library. The latest 2025 CCH work
(https://doi.org/10.1016/j.procs.2025.03.010) likewise requires a
favorable preprocessing/query amortization calculation.

### B: compiled Boolean query microprograms, not approximate retrieval

50,000 queries, of which 31,000 are Boolean and 19,000 exact phrases.
Compile each up-to-6-term **left-to-right** Boolean expression into an
equivalent truth function over six memberships. Represent it as a
64-bit truth table; choose a sparse-list, 64-bit bitmap or AVX2 path
by observed term frequency. Evaluate using batched query panels that
reuse hot postings and decode them once per panel, reducing allocations
and decode traffic. Dedicated exact phrase-anchor logic remains.

Do not assume standard operator precedence. Do not use ranked
top-k pruning, because every answer requires exact count, ID sum and
10 smallest IDs. Existing Q-FUSE and compressed-membership branches
are experimental, not promoted merely because CI passes.

Useful design research: CIDR 2025 MIMD/SIMD interplay
(https://www.vldb.org/cidrdb/2025/rethinking-mimd-simd-interplay-for-analytical-query-processing-in-in-memory-database-engines.html).
Import ideas, not a query-engine library forbidden by contest rules.

### C: partition ownership for exact 100,000-name dictionary

The generator draws from a pool of approximately 100,000 distinct names;
the current shared 131,072-slot table, name arena and country-name bits
already avoid a separate string allocation per record. Prior experiments
varying scanner width, prefetch, hashing, packed names and conditional
atomic bitmap updates did not improve official score.

The next experiment is **metadata-first, partition-owned** exact
interning: cache-resident fingerprint/control bytes identify candidate
full names; exact byte comparisons disambiguate every collision. Assign
hash ranges to independent owners to remove CAS/cache-line ping-pong.
Measure queue/shuffle overhead and peak memory; a separate preprocessing
pass may cost more than 3.32 million probes save. To retain exactness,
do not identify names by a hash fingerprint alone.

Apache Arrow's 2025 hash-join report
(https://arrow.apache.org/blog/2025/07/18/recent-improvements-to-hash-join/)
shows that two-stage partition ownership can remove insertion locks
and that releasing temporary data early cuts peak memory.
Transfer *only* the data-structure principles; the contest bans
using a full join/group-by engine.

Sphinx, PVLDB 2025
(https://doi.org/10.14778/3749646.3749703), examines
succinct perfect-hash indexes; evaluate build-time costs and exact
input-derived index creation before considering these ideas.

### A: avoid repeated hot-loop work on HARP

The HARP hour-prefix cache is a board-verified improvement.
Potential next changes: jointly process 64–128-byte record windows,
minimize expensive hash interner probes for repeated names,
and compare exact three-rank selection against adaptive small-group
sorting and stable two-pass radix selection for large groups.
Percentiles must use nearest rank and preserve 32-bit extreme values.
Do not assume bounded timestamp disorder: preserve the restart-safe
fallback. CPU-local improvements with +memory or +source can lose K.

### Across tracks: microarchitecture-informed deployment

Intel officially specifies i5-13500 as 6 performance cores + 8 efficiency
cores, AVX2, 24 MB cache and up to 76.8 GB/s advertised memory
bandwidth (https://www.intel.com/content/www/us/en/products/sku/230580/intel-core-i513500-processor-24m-cache-up-to-4-80-ghz/specifications.html).
It has no AVX-512. Compare Linux scheduler vs pinned workers *on the
contest-equivalent CPU*; GitHub Actions is an informative but different
machine. Profile stages with `perf stat -e cycles,instructions,branches,branch-misses,cache-misses,minor-faults` where permitted. Detect
false sharing, allocation, NUMA, and P-core/E-core scheduling artifacts.
Do not assume speculative prefetch, huge pages, or BOLT will help.

## Promotion gate (all mandatory)

1. Isolate scope to a single track in an experiment branch, with an exact
   parent and a summary of proposed changes.
2. Validate the organizer's full public output SHA-256; also validate
   generated tiny and at least one different-seed sample/full reference.
3. Benchmark randomized, paired baseline/candidate full executions under
   identical compiler flags. Record runtime and peak RSS; reject noise.
4. Quantify canonical source-size changes, dependencies and score-model
   penalty. No hidden-data assumptions or build-time baking of test inputs.
5. Make one guarded contest-board submission and **check actual points,
   full K axes and gates** before retaining. A CI time improvement alone
   is not enough.
6. Re-read `speed-demon` HEAD before promotion, make only the selected
   file update, and use a merge guard to avoid simultaneous agent writes.
   On any measured regression, restore the highest verified source blob
   rather than revert unrelated agents' contributions.

## Source links

- Contest and official board: https://speeddemon.dev/board/
- Track A source: track-a/main.rs
- Track B source: track-b/main.rs
- Track C source: track-c/main.rs
- Track D source: track-d/main.rs
- Verified scores: BEST_SCORES.md
- Oracle guidance: ORACLE.md

## Later November 2: new score-gated A improvement and 2026 hash research

### A: load-factor-sensitive exact endpoint table wins official score

The exact HARP endpoint interner now uses 8192 hash slots for the same
2048-entry dense-id/arena capacity (old table 4096 slots). Local
[paired official-full benchmark run 41436499988](https://github.com/NewtonRedRing/speed-demon-starter/actions/runs/41436499988)
showed median baseline/candidate **1.02546**, with 25/38 faster pairs;
full public and generated tiny/sample correctness passed.

The [independent scoreboard audit run 41437999219](https://github.com/NewtonRedRing/speed-demon-starter/actions/runs/41437999219)
confirmed the actual contest score improved from **84.036874** to
**84.106702**, runtime **22.229 -> 21.706 ms**, memory
**14,880,768 -> 15,515,648 bytes**, source-size **450 -> 451**.
Production source commit: **0df9114**. This is a verified positive
optimization despite the memory/source penalty and stays in
`speed-demon`.

The experiment reinforces the principle that **shorter probe chains
matter more than tiny table memory differences** on some workloads.
A 16,384-slot version and changes to chunk size were not convincingly
faster in paired CI, so the approved source keeps 8192 slots.

### D: BFS blob size does not change the result

[Official-full SHA-gated CI run 41438100339](https://github.com/NewtonRedRing/speed-demon-starter/actions/runs/41438100339)
benchmarked 10 randomized trials each with 128/256/512/1024/2048/4096
vertex BFS locality blobs. Medians, in ms on the GitHub runner:

| Blob | 128 | 256 | 512 retained | 1024 | 2048 | 4096 |
|---|---:|---:|---:|---:|---:|---:|
| Median | 466.970 | 470.259 | **466.654** | 492.310 | 467.450 | 467.315 |

Peak RSS was essentially unchanged. Combined with the earlier 2.89x
regression when removing renumbering entirely, further tuning blob
size is low priority. Focus on reducing memory accesses and queue
operations inside exact shortest-path queries.

### Cutting-edge CPU hash research: what is actionable

1. **Flattened tiny-pointer tables:** Tang, Mai, Kuszmaul and Conway,
   *Succinct and Fast Tiny Pointer Hash Tables* (July 2026,
   https://arxiv.org/abs/2607.28892). Flattened-TPHT reports
   high performance with a common-case one-cache-miss lookup and
   compact one-byte pointers, with up to 89.3% higher microbenchmark
   throughput than strong baselines. For Tracks A/C, measure a
   control-byte + compact slot index that preserves exact full-key
   comparison and never decodes a fingerprint as the name itself.
   Build and memory cost are counted; no expected contest speedup
   is claimed from their figures.

2. **Swiss-style cache groups:** Elasticsearch's January 2026
   engineering study
   (https://www.elastic.co/search-labs/blog/esql-swiss-hash-stats)
   demonstrates SIMD-readable metadata and cache-line-group probing.
   The reported 2–3x improvements depend on particular
   high-cardinality ingestion workloads. Track C already probes
   a full 64-byte bucket with AVX2; experiment must prove that extra
   metadata reduces misses/atomic contention rather than adding
   overhead. All comparisons remain byte-for-byte exact.

3. **PtrHash** (SEA 2025,
   https://drops.dagstuhl.de/storage/00lipics/lipics-vol338-sea2025/html/LIPIcs.SEA.2025.21/LIPIcs.SEA.2025.21.html)
   is a fast minimal-perfect hash construction and query design.
   Its performance advantage amortizes over many queries on a
   static key set. Track B knows the query term set up front, so
   build-time-per-run MPHF is a possible alternative to its
   current CRC-based dictionary; Track C receives names online
   and would likely pay too much to build an MPHF up front.
   Exact key comparison is still necessary unless an injective
   map is proven.

4. **Road-network exact heuristics:** recent (2026)
   bidirectional-A* road-network work (https://doi.org/10.1016/j.iswa.2026.200641)
   emphasizes admissible lower bounds, but this synthetic track
   includes zero-weight edges, random original node IDs and no
   supplied geometric coordinates. Any proposed A* heuristic
   must be mathematically admissible on every generator seed.
   A non-exact shortcut loses all correctness points.
   Query-preprocessing time must also be included.

### Recommended next experiments

- **A:** look for cache-line/group SIMD index reduction without
  increasing dense-id capacity; any change must beat the 8192-slot
  officially scored code, not an obsolete 4096-slot baseline.
- **B:** compile the six-term strict-left-to-right Boolean expressions
  into exact truth tables; reuse postlists/bitmaps across grouped
  queries and measure whether build and memory costs amortize.
- **C:** collect exact hash-table probe histograms and cache-stall
  data, then test a control-byte metadata layer without dropping
  key comparisons or damaging the current FALCON-C score.
- **D:** profile `Dist::get`, `Dist::at`, and `RadixHeap` by
  query size; investigate exact stale-entry suppression, compact
  queue storage and locality-preserving CSR construction.
  Reject blob-width and unsafe heuristic shortcuts.

No proposed architecture above is promoted without verified full
correctness and an improved official leaderboard score.
