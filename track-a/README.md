# Track A — Ingest

Exact zero-dependency Rust implementation of the official Track A specification.

Twelve workers parse newline-aligned ranges from a read-only mapping. Endpoints
are interned with CRC32C fingerprints plus full byte equality, so hashes are
never treated as semantic identity. Parsed rows are retained as compact
per-hour records. Local endpoint IDs are reconciled once into global bytewise
endpoint order.

Hour work is dynamically scheduled. Each hour counts endpoint membership,
prefix-scatters latencies into one contiguous buffer, then computes exact
nearest-rank p50/p95/p99 using small-group sorting or chained
`select_nth_unstable`. Output is emitted in chronological hour and bytewise
endpoint order.

Dependencies: none.

With another 24 hours: profile parsing/interner cache behavior and percentile
selection on the scoring CPU, retaining only changes that improve full-process
K on exact holdout-style datasets.
