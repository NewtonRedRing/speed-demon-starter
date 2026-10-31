# Speed Demon — 128 separate algorithmic hypotheses screened

**Date:** 2026-10-31. **Status:** source/spec-informed design screening, **NOT** 128 benchmark executions or proven optimality. Each row is an independently described algorithm or processing-architecture variant. The ordinal interest column is an editorial prioritization, never a predicted point gain.

Source baseline is private `NewtonRedRing/speed-demon-starter` branch `speed-demon` as inspected on October 31. All choices must be evaluated on the pinned GCC 15 / i5-13500 official harness, checked against full seed-1 SHA-256 and multiple non-public-seed-style generator workloads.

## Screening accounting

| Track | Top candidates | Trial candidates | Already implemented | Deferred | Rejected | Total |
|---|---:|---:|---:|---:|---:|---:|
| A | 2 | 12 | 7 | 6 | 5 | 32 |
| B | 6 | 13 | 5 | 1 | 7 | 32 |
| C | 4 | 11 | 6 | 3 | 8 | 32 |
| D | 3 | 10 | 7 | 3 | 9 | 32 |

## 128 screened variants (A: 1–32, B: 33–64, C: 65–96, D: 97–128)

### Track A

| Iteration | Subsystem | Candidate | Screening | Reason |
|---:|---|---|---|---|
| 1 | parse | `dual-independent-row-lanes` | **TRIAL** | Break dependency chains in existing fused SWAR parser; may cost i-cache |
| 2 | parse | `AVX2-batched-field-boundary-masks` | **DEFER** | Variable endpoints make SIMD cross-record integration expensive |
| 3 | parse | `single-row-SWAR-plus-CRC` | **BASELINE** | Already present; do not duplicate existing work |
| 4 | parse | `branchless-timestamp-byte-decoding` | **TRIAL** | Fixed-format timestamp but already 3 direct byte reads |
| 5 | endpoint-ID | `per-worker-SIMD-fingerprint-bucket` | **TRIAL** | Compare multiple candidate fingerprints before exact byte equality |
| 6 | endpoint-ID | `hot-cold-query-independent-dictionary` | **TRIAL** | Dynamic endpoint recurrence; preserve generic collision semantics |
| 7 | endpoint-ID | `full-trie-endpoint-classifier` | **DEFER** | Unseen seed and dynamic endpoint lengths make trie expensive to build |
| 8 | endpoint-ID | `string-dictionary-std-unordered-map` | **REJECT** | Per-record allocations and pointer chasing vs compact table |
| 9 | record-layout | `six-byte-AoS-record` | **BASELINE** | Already packed as u32 latency plus u16 endpoint/status |
| 10 | record-layout | `separate-latency-and-ID-SoA` | **TRIAL** | Potential vectorized quantile reads at cost of extra stream |
| 11 | record-layout | `chunk-page-hour-block-arena` | **TRIAL** | Avoid repeated vector capacity checks and moves |
| 12 | record-layout | `full-aligned-eight-byte-record` | **DEFER** | Simpler aligned loads but 33% more record bytes |
| 13 | partition | `hour-local-count-prefix-scatter` | **BASELINE** | Already removes full-size secondary latency buffer |
| 14 | partition | `American-flag-in-place-partition` | **TRIAL** | Save scattering copies but pay random-write swaps |
| 15 | partition | `stable-three-stage-counting` | **DEFER** | More passes likely exceed saved work |
| 16 | partition | `per-endpoint-direct-shard-append` | **REJECT** | Millions of tiny per-group buffers and allocator overhead |
| 17 | quantiles | `shared-hierarchical-radix-multiselect` | **TOP** | Exact three ranks share uint32 histogram tree for large groups |
| 18 | quantiles | `adaptive-introspective-three-way-multiselect` | **TOP** | Select all three ranks in one recursive partition tree, preserving duplicates |
| 19 | quantiles | `sampled-pivot-select-with-exact-fallback` | **TRIAL** | Use approximate pivot only to accelerate exact final partition |
| 20 | quantiles | `full-sort-for-all-group-sizes` | **REJECT** | Excess comparisons on large groups; already avoided |
| 21 | parallel | `hour-task-dynamic-stealing` | **BASELINE** | Already task distributed by 72 hours |
| 22 | parallel | `split-large-hours-into-independent-groups` | **TRIAL** | Fight skew where slowest hour holds overall completion |
| 23 | parallel | `split-input-by-scanline-shards` | **BASELINE** | Already uses 12 byte-aligned worker ranges |
| 24 | parallel | `pin-threads-to-P-core-hyperthreads` | **TRIAL** | Reduce core migration only if legal and tested on scoring box |
| 25 | format | `per-hour-right-sized-output` | **BASELINE** | Already implemented |
| 26 | format | `single-writer-writev-aggregation` | **TRIAL** | Reduce small writes without waiting on slow producer tasks |
| 27 | format | `direct-stream-group-output` | **DEFER** | Chronological ordering and straggler workers restrict streaming |
| 28 | format | `sprintf-per-statistic` | **REJECT** | General formatting overhead |
| 29 | memory | `madvise-consumed-complete-pages` | **BASELINE** | Already measured retained behavior |
| 30 | memory | `larger-page-release-granularity` | **TRIAL** | Balance madvise syscalls versus cgroup page charges |
| 31 | memory | `disable-page-release` | **DEFER** | Previously larger RSS; inconsistent full score impact |
| 32 | memory | `whole-file-private-copy` | **REJECT** | Copies 239MB and worsens memory |

### Track B

| Iteration | Subsystem | Candidate | Screening | Reason |
|---:|---|---|---|---|
| 33 | phrases | `rare-anchor-full-token-window-verification` | **TOP** | Avoid automaton state updates on most tokens, verify exact token window |
| 34 | phrases | `frequency-hybrid-anchor-plus-Aho-Corasick` | **TOP** | Route common-anchor phrases to AC, rare anchors to candidate verifier |
| 35 | phrases | `anchor-pair-two-term-prefilter` | **TRIAL** | Reject candidates by second anchor before full verification |
| 36 | phrases | `full-token-Aho-Corasick-only` | **BASELINE** | Existing implementation; O(tokens) state updates |
| 37 | dictionary | `SIMD-probed-open-address-term-table` | **TRIAL** | Batch control bytes, then full token equality |
| 38 | dictionary | `two-tier-length-1-to-3-direct-ID` | **TRIAL** | Only helps if short tokens common in real corpus |
| 39 | dictionary | `chunked-64-bit-hash-fingerprint` | **TRIAL** | Previous isolated hash benchmark promising, full scan unproven |
| 40 | dictionary | `per-token-unordered_map-string` | **REJECT** | Allocations and pointer chasing |
| 41 | tokenizer | `SWAR-ASCII-word-boundary-scanner` | **TOP** | Massively repeated 36M-token stage, safely discard all high bytes |
| 42 | tokenizer | `AVX2-32byte-token-lane-classifier` | **TRIAL** | Build alnum masks and lowercase ASCII exactly |
| 43 | tokenizer | `stateful-skip-fast-nonquery-token` | **TRIAL** | Query-only dictionary; avoid expensive checks after impossible prefix |
| 44 | tokenizer | `UTF8-casefold-normalized-tokenizer` | **REJECT** | Violates byte-based ASCII spec |
| 45 | boolean-hits | `worker-local-flat-hit-array` | **BASELINE** | Already implemented |
| 46 | boolean-hits | `packed-doc-ID-18bit-with-term-deltas` | **TRIAL** | 250k docs fit 18 bits; packing may cost decode |
| 47 | boolean-hits | `block-local-sparse-hit-delta` | **TRIAL** | Reduce memory traffic at cost of encoding |
| 48 | boolean-hits | `global-per-hit-atomic-postings` | **REJECT** | Millions of contended random atomics |
| 49 | postings | `adaptive-sparse-dense-current` | **BASELINE** | Already adaptive |
| 50 | postings | `roaring-style-array-bitset-containers` | **TRIAL** | Per-block density could beat global choice; build cost risk |
| 51 | postings | `SIMD-sorted-intersections` | **TRIAL** | May help AND but queries stage already short |
| 52 | postings | `unconditional-all-doc-bitmaps` | **REJECT** | ~53k x 250k bits very large, source size |
| 53 | queries | `per-query-bitmap-word-on-demand` | **TRIAL** | Evaluate dense Boolean only on touched document words |
| 54 | queries | `rare-first-AND-reorder-with-proof` | **DEFER** | Strict left-to-right operators disallow arbitrary rearrangement |
| 55 | queries | `candidate-doc-short-circuit-boolean` | **TRIAL** | Evaluate each candidate only if index cardinality small |
| 56 | queries | `Boolean-OR/NOT-commute-reorder` | **REJECT** | Changes left-associative semantics |
| 57 | parallel | `thread-local-doc-packet-dynamic-queue` | **TRIAL** | Reduce load imbalance from long document lengths |
| 58 | parallel | `static-sharded-corpus-scanning` | **BASELINE** | Already split 12 aligned byte chunks |
| 59 | parallel | `build-phrase-anchor-index-once` | **TOP** | Amortize anchor lookup and share token stream with Boolean hits |
| 60 | parallel | `serialize-all-scanning` | **REJECT** | Reduces parallel utilization |
| 61 | memory | `token-vector-reuse-per-document` | **TOP** | Avoid positional postings, only longest document tokens ~1k |
| 62 | memory | `ring-buffer-maxphrase-length-with-delayed-verify` | **TOP** | Bound temporary document token memory and verify candidates after lookahead |
| 63 | memory | `retain-all-document-token-streams` | **REJECT** | >36M token ids ~145MB plus build pressure |
| 64 | memory | `madvise-1MiB-consumed` | **BASELINE** | Already releases mapped pages |

### Track C

| Iteration | Subsystem | Candidate | Screening | Reason |
|---:|---|---|---|---|
| 65 | csv-structure | `hybrid-memchr-quote-plus-SIMD-unquoted` | **TOP** | Quoted-heavy corpus warrants different search modes |
| 66 | csv-structure | `AVX2-parity-mask-complete-CSV` | **TRIAL** | Correct doubled quotes and LF/BOM boundaries before using masks |
| 67 | csv-structure | `bytewise-state-machine-only` | **BASELINE** | Current exact implementation uses memchr where useful |
| 68 | csv-structure | `split-on-newline-unconditionally` | **REJECT** | Embedded LF inside quoted notes invalidates record boundaries |
| 69 | boundary | `mapped-quote-parity-prepass` | **TRIAL** | Avoid buffered pread copy, but page cache effect unknown |
| 70 | boundary | `SIMD-quote-parity-XOR` | **TRIAL** | Can speed boundary phase; only ~10ms historically |
| 71 | boundary | `buffered-parity-current` | **BASELINE** | Existing proven approach |
| 72 | boundary | `partition-by-raw-LF-byte` | **REJECT** | Multiline notes break correctness |
| 73 | region-join | `mapped-dense-lookup-loader` | **TRIAL** | Previously only isolated loader gain |
| 74 | region-join | `dense-u8-region-ID-array-current` | **BASELINE** | Already direct country lookup |
| 75 | region-join | `binary-search-country-records-per-row` | **REJECT** | 3.32M repeated searches instead of O(1) |
| 76 | region-join | `region-streaming-fused-amount-parser` | **TRIAL** | Minimize passes and intermediate fields |
| 77 | names | `worker-local-exact-name-ID-plus-country-mask-merge` | **TOP** | Avoid atomic shared table in 3.32M hot loop, merge ~100k exact names |
| 78 | names | `partition-pairs-by-name-hash-then-dedup` | **TOP** | Sequential owner-local distinct reduction; track bytewise equality |
| 79 | names | `shared-striped-membership-dictionary` | **BASELINE** | Already present with atomic 240-bit country masks |
| 80 | names | `global-unbounded-string-set-with-lock` | **REJECT** | Heavy global contention |
| 81 | dedup-form | `global-dictionary-plus-4x64-country-bits` | **TOP** | Compact exact membership; cost if global insert contention |
| 82 | dedup-form | `sort-unique-(hash,country,name)-pairs` | **TRIAL** | Dense gather then parallel radix/sort; extra 3.32M records |
| 83 | dedup-form | `run-length-encode-per-country-names` | **DEFER** | Not naturally grouped in input |
| 84 | dedup-form | `HyperLogLog-distinct-estimate` | **REJECT** | Output requires exact distinct counts |
| 85 | lookup-hash | `CRC32C-name-fingerprint-with-full-equality` | **TRIAL** | Checksum fast on scoring CPU; exact fallback mandatory |
| 86 | lookup-hash | `SIMD-prefix-equality-before-full-string` | **TRIAL** | Avoid cache miss on mismatches; 100k distinct names |
| 87 | lookup-hash | `dynamic-minimal-perfect-hash` | **DEFER** | Building MPH online in one pass is expensive |
| 88 | lookup-hash | `fingerprint-only-distinct` | **REJECT** | Collisions produce incorrect results |
| 89 | parallel | `stripe-owner-batched-requests` | **TRIAL** | Reduce mutex contention but add queue traffic |
| 90 | parallel | `worker-private-numeric-aggregates` | **BASELINE** | Already employed |
| 91 | parallel | `six-vs-twelve-thread-dynamic` | **TRIAL** | Physical P-core occupancy and lock contention tradeoffs |
| 92 | parallel | `single-thread-join` | **REJECT** | Loses multicore throughput |
| 93 | format-memory | `small-string-inline-names` | **TRIAL** | Avoid heap per unique name if names short |
| 94 | format-memory | `hold-mapped-name-string_views-only` | **DEFER** | Madvise releases input pages; lifetime/copy semantics complex |
| 95 | format-memory | `zero-copy-unescaped-quoted-names` | **BASELINE** | Most names already not materialized |
| 96 | format-memory | `strtod-float-cents` | **REJECT** | Must be exact integer cents; floating point unnecessary |

### Track D

| Iteration | Subsystem | Candidate | Screening | Reason |
|---:|---|---|---|---|
| 97 | graph-parse | `two-pass-shared-mmap-scanner` | **TRIAL** | Remove fread copies; earlier isolated gains not full-program |
| 98 | graph-parse | `parallel-block-file-buffer-current` | **BASELINE** | Existing two-pass 12-worker parser |
| 99 | graph-parse | `one-pass-edge-log-then-scatter` | **TOP** | Parse each edge once; additional raw-edge memory + scatter |
| 100 | graph-parse | `iostream-extraction-every-edge` | **REJECT** | Expensive 5M formatted extractions |
| 101 | degree-build | `private-tile-degree-count-reduction` | **TRIAL** | Trade one reduction for less atomic contention |
| 102 | degree-build | `global-relaxed-atomic-degrees-current` | **BASELINE** | Already tested fast |
| 103 | degree-build | `non-atomic-thread-private-full-n-arrays` | **DEFER** | Prior experiment >300MB and slower |
| 104 | degree-build | `sampled-degree-prediction-with-correction` | **REJECT** | Exact offsets still require counting all edges |
| 105 | CSR-storage | `compact-6byte-edges-plus-overflow` | **BASELINE** | Already uses u16+wide uint32 sparse map |
| 106 | CSR-storage | `8byte-aligned-u32-weight-edge` | **TRIAL** | Higher bytes, faster aligned loads; earlier full width lost |
| 107 | CSR-storage | `uint32-weight-inline-outlier-table` | **TRIAL** | Avoid unordered_map.at branch on rare heavy edges |
| 108 | CSR-storage | `varint-delta-target-IDs` | **REJECT** | Input node IDs randomly relabeled; poor locality |
| 109 | priority-queue | `two-tier-short-weight-Dial-plus-radix-overflow` | **TOP** | Main edges <~1000 while occasional u32 edges preserved |
| 110 | priority-queue | `d-ary-small-heap-for-local-search` | **TRIAL** | Small query frontiers may favor binary/4-ary heap |
| 111 | priority-queue | `packed-64bit-monotone-radix-current` | **BASELINE** | Already exact with 12byte entries |
| 112 | priority-queue | `Fibonacci-heap-for-bidirectional` | **REJECT** | Pointer-heavy; decrease-key overhead |
| 113 | query-policy | `forward-first-small-settled-budget` | **TOP** | Query target sampled by Dijkstra rank, many close; exact fallback |
| 114 | query-policy | `degree-weighted-bidirectional-side-choice` | **TRIAL** | Select side by expected relaxation work vs queue count |
| 115 | query-policy | `balance-by-queue-size-current` | **BASELINE** | Already implemented |
| 116 | query-policy | `nearest-Euclidean-landmark-from-node-ID` | **REJECT** | Node IDs relabeled; no coordinate information |
| 117 | distance-map | `per-worker-generation-stamped-sparse-table` | **BASELINE** | Already in use |
| 118 | distance-map | `paged-direct-index-dist-stamp-arrays` | **TRIAL** | Avoid hash probes for small searches, but pages huge |
| 119 | distance-map | `hybrid-hot-sparse-then-dense` | **TRIAL** | Switch only for high-degree queries |
| 120 | distance-map | `clear-entire-n-distance-array-each-query` | **REJECT** | 2k*1.74M initialization writes expensive |
| 121 | query-batch | `memoize-duplicate-(source,target)-pairs` | **TRIAL** | Generator includes 1% duplicate queries |
| 122 | query-batch | `precompute-zero-outdegree-unreachable` | **BASELINE** | Already early rejects query endpoints |
| 123 | query-batch | `shared-source-multitarget-search` | **DEFER** | Very few duplicate sources among 2000 random pairs |
| 124 | query-batch | `all-pairs-shortest-path` | **REJECT** | Quadratic memory/time impossible |
| 125 | preprocessing | `SCC/weak-component-fast-unreachable-rejection` | **TRIAL** | Tiny islands yield some negative queries; build cost risk |
| 126 | preprocessing | `bounded-local-shortcuts-from-highway-vertices` | **DEFER** | Unclear amortization with 2000 local queries |
| 127 | preprocessing | `full-contraction-hierarchy-in-run` | **REJECT** | Build time likely dwarfs ~250ms query stage |
| 128 | preprocessing | `landmark-ALT-preprocess-all-main-vertices` | **REJECT** | Random relabeling and preprocessing cost; validate only if huge query count |

## Evidence and decision boundary

This report contains **128 separate, documented screen decisions**, not 128 compilation or official timing experiments. A `TOP` label means prioritize prototype + differential correctness testing; it does not imply performance superiority. `BASELINE` means already implemented in the current production algorithm; `REJECT` means a well-grounded structural or exactness failure; `DEFER` means cost or workload mismatch; `TRIAL` means plausible but requires measured evidence.

Small independent mathematical oracles are in `oracle_proofs.py`: unsigned u32 radix selection vs full sort (1,017 cases), anchored phrase verification vs exhaustive scans (180 docs/325 phrases), exact pair deduplication vs bitmask dictionary (50,000 events), and shortest-path query agreement including zero and full-width u32 weights (6,000 random queries). **These are correctness experiments, not speed measurements or full official hash tests.**

## Architecture finalists

1. **A:** Hybrid existing SIMD parser and 6-byte records + exact shared-rank multiselection (MSD radix or multikey quickselect only for sufficiently large skewed groups). Test thresholds 33/64/128/256/512/1,024+, including duplicates and max-u32. For small groups preserve existing exact AVX2 sorting networks.
2. **B:** Query-first term dictionary + document-local token ring/window + rare-anchor phrase verification, with AC fallback for common anchors. Keep worker-local Boolean hit compaction. Every candidate must verify full token IDs/bytes, not hash-only; stamp matches per phrase per document. This changes per-token automaton work and is the highest asymptotic upside.
3. **C:** Existing quote-safe parser plus worker-private exact `(decoded_name, country)` dedup, followed by ownership-based name-hash partition and exact reconciliation. Compare against shared striped atomic dictionary; this adds a second pass over at most one unique pair per worker-country-name, and extra temporary storage.
4. **D:** Two-pass compact CSR plus hybrid short-forward Dijkstra/ongoing exact bidirectional search, testing degree-based frontier side choice and two-tier weight queues. Preserve u64 distance; avoid CH/ALT unless full within-run preprocessing cost is demonstrably paid back.

## Score and compliance

Use `K=(Tref/T)^0.5 * min(Mref/M,50)^0.2 * min(Sref/S,1)^0.15 * (Dref/D)^0.15`; 90 machine points requires `K>=63.095734`. The official process measurement is based on an AVX2 i5-13500, six P-cores (12 logical processors), an 8GB cap and five serialized runs. Source size is formatter-counted, dependency count matters. Optimize K, not solely wall time. Never infer a hidden-seed result from microbenchmarks.

## External research references

- Event specifications/scoring/hardware: https://speeddemon.dev/
- Official source/generators: https://github.com/hackathon-raptors/speed-demon-2026
- SIMD CSV split versus quoted/unquoted workload: https://github.com/medialab/simd-csv
- Adaptive compressed postings: https://github.com/RoaringBitmap/CRoaring
- Fast vector integer postings: https://github.com/fast-pack/FastPFor
- Query-efficient but potentially slow within-run CH: https://github.com/RoutingKit/RoutingKit/blob/master/doc/ContractionHierarchy.md
- Parallel CH construction study (construction cost is crucial): https://doi.org/10.1145/3721145.3725744
- SWAR decimal token parsing: https://lemire.me/blog/2022/01/