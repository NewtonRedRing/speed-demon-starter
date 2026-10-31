# Track A: Ingest

The task, the input files and the exact output format are in the [spec](https://github.com/hackathon-raptors/speed-demon-2026/blob/main/specs/a-ingest.md).

## Approach

Twelve aligned byte ranges parse a shared read-only mapping (one worker for
small inputs). Word-at-a-time delimiter masks skip methods and fuse endpoint
scanning with CRC32C fingerprints. Every fingerprint match still requires full
string equality. Bounded word loads parse decimal latencies in parallel byte
lanes; a scalar tail preserves the complete unsigned 32-bit range. Consumed
complete pages are released in 1 MiB batches, without releasing a neighboring
worker's partial page. Removing these calls did not reliably improve speed.

Each worker appends six-byte records to per-hour contiguous vectors: uint32
latency plus eleven endpoint bits and one error bit. It no longer updates
random hour/group counters while parsing. Endpoint IDs are reconciled once
and remapped into bytewise lexical order.

Twelve dynamic hour tasks reconstruct counts in small local tables, prefix
and scatter into one hour-local uint32 array, and release the source records.
There is no full-dataset secondary latency array or shared group table.
Each task owns its storage; no per-record atomics are needed.

Singletons and pairs use direct exact formulas. An AVX2 unsigned sorting network
handles groups of three to eight, with masked loads/stores and UINT32_MAX
padding. Groups of nine to 32 use `std::sort`; larger groups use three ordered
`nth_element` selections for exact nearest-rank p50/p95/p99.
Output capacities depend on present groups and endpoint lengths rather than
the entire catalogue. Completed hour buffers are written in chronological
hour and bytewise endpoint order. Status codes 500-599 are errors. No input
ordering bound, cached answer or seed-specific endpoint list is assumed.

## Dependencies

None.

## With another 24 hours

Profile this design on the scoring CPU and obtain an official run ID. Investigate
two-stream parsing or radix multiselect only with complete-executable evidence.
An in-place record partition was not implemented: hour-local scatter already
eliminates the dataset-sized secondary array. Six/eight workers, a larger SIMD
network, ordered streaming writes and two-seed PGO did not justify retention
in the tested variants.

Latest eight-run alternating full seed-1 comparison against commit `60e1b80`:
0.05048445 s before versus 0.04034590 s after, **20.1% lower median runtime**.
Median fresh-cgroup peaks were 69.53 versus 36.39 MiB, **47.7% lower**.
Three comparison batches measured 19.3-28.8% runtime reductions; host load
prevents cross-batch timing comparisons. Input pages were prewarmed by another
live cgroup. The memory counter includes a small static measurement supervisor,
and is not an official board measurement. RSS is recorded separately, not
substituted for the scoring memory counter.

All 15 regression tests, three A sanitizer oracle methods, two additional
official-generator sample seeds checked against the original reference, strict
AVX2-only compiler checks and the official full-data production sandbox pass.
Developed on `experiment/track-a-speed-first`, the validated changes are also
promoted to `speed-demon`. There is no verified 90-point or leaderboard win.
See [PERF.md](PERF.md) for individual experiments and limitations.

The hardware limits are ceilings: one process uses twelve worker threads, not
512 competing processes. Memory is used for contiguous records, group storage
and output where it avoids work; allocating the rest of the 8 GB adds no speed.
Latest whole-process average CPU concurrency was about 7.4 CPU-equivalents,
not continuous use of all twelve. Input/output and endpoint reconciliation still
impose serial work. The local Docker host is not the six-P-core, turbo-disabled
scoring machine.
