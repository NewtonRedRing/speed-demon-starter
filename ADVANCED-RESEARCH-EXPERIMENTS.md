# Exact Algorithms for Speed Demon 2026 — experimental research record

**Branch:** `experiments/90-plus-exact-20261030`. **Status:** unvalidated for
integrated/full-scale correctness, measured speed, or official score.
**Do not merge to `speed-demon` without a full SHA-256 pass.**

## Official data and reproducibility

Official public full seed-1 data, 790.5 MB total:
https://speeddemon.dev/board/#datasets

Manifest:
https://s3.amazonaws.com/raptors.dev/speeddemon.dev/2026/data/SHA256SUMS.json

Run `python3 scripts/public_full_benchmark.py --tracks a b c d`.
The script downloads all original inputs, verifies every input checksum, builds
each Docker image using `check.py`, and compares the output SHA-256 against the
official seed-1 hash. Run `--download-only` for input verification only.
Keep data under `.data/`, ignored by git. No precomputed input-dependent
artifacts may be embedded in a submission image; final scoring uses an unseen seed.

Expected full output digests (official manifest):
* A: `495ca09682b9673ef836a506496177680bb4a3af54255174befddb1f83481cd3`
* B: `f86973c210228141bcb6f1f1b10188ad2a9b65f71e7cd7cf4590b38de4e80927`
* C: `d8088b1cd42ba1338b1345b107b55db7b96ef1f5066f3ff64fd26a8912a0f536`
* D: `a23dbdf0a3e49cc8901310a9ee1dc6a9a569a85297b6da75c2a7002cb2ed277e`

## Algorithm family developed (exact, input-independent)

### Track A: adaptive order statistics
Full sort for small groups; exact `nth_element` at r99, then r95 in
the lower subrange, then r50, retaining nearest-rank semantics with ties.
One MiB I/O buffers, no change to grouping or output. All group order
statistics are exact (not estimates). Measure first: the earlier board
baseline was already very fast (around 0.63 s).

### Track B: sparse/dense query-aware index
Read queries first; index only requested terms. Only phrase terms require
positions; Boolean-only tokens receive sorted document IDs but no positions.
Build document postings during the source scan, avoiding a redundant
postprocessing scan. Boolean AND uses binary searches when posting lengths
are badly imbalanced; balanced cases use a linear merge. NOT reserves memory
based on left-side cardinality. Phrase position checks are exact.

Future experiment: build a short-lived document-wise token ID stream only
for rare phrase anchors, and benchmark against full per-term positional lists.
If memory dominates, consider SIMD differential posting compression (FastPFor)
but weigh extra code/dependencies and actual decode time.

### Track C: Global Name-ID / Country Bitset Join (GNCBJ)
Exact global byte-string dictionary gives each distinct decoded name a
32-bit ID. The per-country membership structure is a growing bit vector:
`seen[country][id / 64] & (1ULL << (id % 64))`. On first set, increment
country's distinct count. The single global dictionary stores string_views
into immutable input bytes when quoted CSV contains no doubled quote;
decode and retain a stable owned string only when doubled quote escapes
occur. All length and distinct semantics remain byte-exact. This replaces
one separate unordered_set<string> per country with one dictionary and
dense bit vectors, reducing allocator traffic and allowing fast membership.

Important cases: doubled quote escapes, empty quoted/unquoted name equivalence,
trailing spaces, mid-file BOMs, mixed LF/CRLF, embedded LF in note, negative
cents, unmatched regions, and countries with zero records.

### Track D: Six-worker Query-Adaptive Shortest-Path Batch (6QASB)
Queries grouped by source; disjoint source groups are assigned through an
atomic work queue to up to six workers. Each worker has its own 64-bit
distances, generation stamps, and four-ary min-heap. Edges and CSR offsets
are immutable and shared. Work stealing at group granularity balances
very short and very long local queries. Each worker answers different
output indices, so no locks on answers are needed; write output in input
order after joins. Handles weight 0, parallel edges, disconnected islands,
and distances above UINT32_MAX. This is exact Dijkstra with coarse-grained
parallelism, *not* an approximation or a claimed new shortest-path theorem.

Future experiment: bidirectional Dijkstra on forward/reverse CSR
with rigorous termination when `min_forward + min_reverse >= best_path`,
versus current one-direction local search, including exact handling of
directed asymmetric edges, zero edges and unreachable targets. For batches
of 2,000 local queries, a large contraction-hierarchy or ALT preprocessor
may cost more than it saves; benchmark rather than assuming gains.

## Related primary research / code

1. KIT RoutingKit, CH/CCH for exact road routing:
   https://github.com/RoutingKit/RoutingKit
2. Goldberg and Harrelson, *Computing the Shortest Path: A* Search Meets
   Graph Theory* (directed ALT, 2004):
   https://www.microsoft.com/en-us/research/publication/computing-the-shortest-path-a-search-meets-graph-theory/
3. KIT, *Fast and Accurate Route Planning* (CH family):
   https://ae.iti.kit.edu/english/479.php
4. FastPFor and SIMDCompressionAndIntersection:
   https://github.com/fast-pack/FastPFor
   https://github.com/lemire/SIMDCompressionAndIntersection
5. LICO, SIGMOD 2026 research on learned SIMD inverted index:
   https://github.com/xianyuzhuruc/LICO
   Not a direct plug-in: build/preprocessing costs and target CPU features vary.
6. Langdale/Lemire (2019), *Parsing Gigabytes of JSON per Second*;
   analogous staged structural scanning ideas, not a CSV parser replacement:
   https://simdjson.org/publications/
7. HKUST 2019, *Batch Processing of Shortest Path Queries in Road Networks*:
   https://repository.hkust.edu.hk/ir/Record/1783.1-108077

## Gate for adopting any optimization

1. `g++ -std=c++20 -O3 -DNDEBUG -flto` compiles under the pinned offline
   Dockerfile and under GCC 15 as used by the competition.
2. Randomized tiny seeds 1/2 plus official sample seed 1 pass byte-identical
   differential outputs compared with the original `speed-demon` branch.
3. All four full datasets from the published S3 links have matching input SHA.
4. All four candidate output SHA checks match the official full manifest.
5. Repeated relative timing and peak memory are better, accounting for the
   `scc` source-line penalty and dependency penalty.
6. Official live-board score improves after a controlled change to the
   scoring branch, preserving prior good commit for rollback; record its run ID.
7. A 90+ *total track score* includes a judged 35% component and cannot
   be inferred solely from our local microbenchmarks.

GitHub Actions on the original experiment repeatedly failed with no runner
assigned or executed steps; that is **not** a successful test, and the jobs
provided no compilation or output hashes. The direct full-data script is the
primary verification path until runner access is functional.

## Locally executed kernel tests (NOT whole-program validation)

A standalone C++20 program compiled with GCC 14 and `-O2` passed:
- 2,000,000 randomized four-way-heap operations vs `std::priority_queue`.
- 10,000 randomized adaptive posting intersections vs `std::set_intersection`.
- 500,000 randomized name/country membership events vs `unordered_set<string>`.
- 20,000 randomized nearest-rank percentile cases vs full sorting.

A separate standalone C++20 `-O3 -march=native` microbenchmark,
reproducible via `tests/benchmark_country_bitsets.cpp`, generated 3,320,000
name/country events (240 countries, 100,000 names) and returned the exact same
3,100,639 country/name memberships in both approaches:
- Per-country `unordered_set<string_view>`: 4.40 seconds, 215,168 KB peak RSS.
- Global name-ID + country bitsets: 0.57 seconds, 40,192 KB peak RSS.

These numbers reflect an **isolated synthetic membership kernel on a different
machine**, not the official real CSV distribution, memory of the full track,
competition-scored time, or a guarantee of 90+ points. Real parser, hash-map
and I/O costs could reduce end-to-end speedup substantially.

## Repository-wide prototype audit

The repository contains alternate prototype implementations under each
`track-*/src/track_*_*.cpp`, but **none of them are linked by the pinned
Dockerfiles**, which copy only `src/main.cpp`.

- A prototype: parallel range processing, radix/bucket-oriented ingestion,
  much larger source.
- B prototype: compact postings, query buckets, multi-worker indexing and
  custom string hashing.
- C prototype: parallel CSV block and shard processing, separate aggregation.
- D prototype: forward/reverse graph CSR, monotone radix priority queue,
  degree-aware bidirectional search, connected-component pruning and a
  separate wide-distance fallback path.

These are **candidate architectures to benchmark, not validated drop-in
replacements**. In particular, D's prototype uses UINT32_MAX as an
unreached sentinel in fast paths; the official D graph deliberately includes
shortest paths exceeding UINT32_MAX, so its separate wide-distance fallback
must be tested before reuse. And importing the large prototype sources
unselectively can worsen the scc source-line penalty.

The current experiment implements independently parallel exact 64-bit
single-source shortest paths instead. Compare the existing bidirectional
radix prototype as a *separate* validated experiment before changing it.
