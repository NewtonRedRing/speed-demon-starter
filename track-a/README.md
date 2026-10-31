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

## 2026-10-31: CPU-set portability and endpoint probing

The worker count now uses `available_parallelism()`, capped at the 12 hardware
threads provided by the official scorer. On smaller local CPU sets this avoids
oversubscription; on the scoring machine it retains all twelve workers.
Endpoint hash tables use 8,192 slots with power-of-two masking to reduce
collisions, but the packed record format still has an explicit 2,048-local-ID
limit. The generator specifies 1,842 distinct endpoints; a larger catalogue
would require a wider record encoding rather than just a larger hash table.

The existing offline, digest-pinned, multi-seed Rust PGO Docker build is
preserved. No source-hiding `build.rs` or `.inc` indirection is used: the
2026-10-28 scoring rules cap the source-size benefit at 1×, and the
`scc` count covers all source under this track's build context.
