# Track A: Ingest

The task, the input files and the exact output format are in the [spec](https://github.com/hackathon-raptors/speed-demon-2026/blob/main/specs/a-ingest.md).

## Approach

Twelve aligned byte ranges stream through bounded buffers (one worker for small
inputs). Small initial latency allocations avoid reserving oversized vectors
for sparse groups. Each worker interns exact endpoint strings in a fingerprinted table;
hash matches always require full string equality. Thread-local groups retain
uint32 latencies and error counts. After merging, three ordered `nth_element`
selections compute exact nearest-rank p50/p95/p99 without fully sorting large
groups. Groups with at most 32 values use a small full sort instead.
Per-hour output is formatted in parallel and written in deterministic
chronological-hour and bytewise-endpoint order. Status codes 500–599 are errors.

## Dependencies

None.

## With another 24 hours

Profile streaming and merge allocations on the scoring CPU. Current local full
seed-1 median: 0.064004 s, 40.90 MiB peak RSS; no official run ID is available
for this candidate. See [PERF.md](PERF.md) for validation and limitations.
