# Track A: Ingest

The task, the input files and the exact output format are in the [spec](https://github.com/hackathon-raptors/speed-demon-2026/blob/main/specs/a-ingest.md).

## Approach

Twelve aligned byte ranges parse a shared read-only mapping (one worker for
small inputs). The parser advances directly from latency to the next record,
without separately searching for every newline. Endpoint delimiters use
`memchr`; an inline word-at-a-time hash fingerprints every endpoint byte, and
fingerprint matches always require full string equality. Consumed
complete pages are released in 1 MiB batches without releasing a neighboring
worker's partial page.

Each worker appends compact latency/key records to per-hour contiguous vectors
and counts groups in a compact hour-major table. There are no per-group latency
vectors. Endpoint IDs are reconciled once and remapped into bytewise lexical
order. A prefix over hour sizes assigns disjoint slices of one uint32 latency
array. Twelve workers dynamically claim hour tasks; each task counts its groups,
plans offsets, scatters, selects and formats without an intervening global
barrier. Hour tasks write disjoint storage and need no per-record atomics.

Three ordered `nth_element` selections compute exact nearest-rank p50/p95/p99
without fully sorting large groups. Groups with at most 32 values use a small
full sort instead. Per-hour output uses bounded direct buffers and `to_chars`,
then is written in deterministic chronological-hour and endpoint order.
Status codes 500–599 are errors. No ordering bound on the input is assumed.

## Dependencies

None.

## With another 24 hours

Profile endpoint hashing and parsing on the scoring CPU. Twelve-worker speed
was better than six or eight locally. Larger endpoint tables, custom small
sorts and fixed-method dispatch did not justify retention.

Latest local full seed-1 median: 0.045975 s, 65.67 MiB peak RSS over five runs.
The first redesign measured 34.3% lower elapsed time than the published source.
The subsequent fused pipeline reduced a controlled median from 0.043447 s to
0.042055 s (another 3.2%) and slightly reduced RSS. This speed-first candidate
has no official run ID or verified score improvement.
The later inline endpoint hash reduced its immediate baseline's latest
controlled median from 0.054506 s to 0.052878 s (3.0%). It does not replace
full equality checks or assume collision-free hashing.
See [PERF.md](PERF.md) for validation and limitations.

The hardware limits are ceilings: one process uses twelve worker threads, not
512 competing processes. Memory is used for contiguous records, group storage
and output where it avoids work; allocating the rest of the 8 GB adds no speed.
Local whole-process average CPU concurrency measured about 7.7 CPU-equivalents,
not continuous use of all twelve. Input/output and endpoint reconciliation
still impose serial work. The local Docker host is not the six-P-core,
turbo-disabled scoring machine.
