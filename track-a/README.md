# Track A — Ingest

The Rust executable maps `requests.log`, splits work at line boundaries, identifies hours without full calendar parsing and packs endpoint/latency/error values into four-byte records. Shared endpoint interning uses vectorized byte comparisons. Parallel hour aggregation computes the nearest-rank p50/p95/p99 and outputs exact bytewise-sorted groups.

- Language: Rust, no external runtime dependencies
- Inputs: `/data/requests.log`
- Output: `/out/summary.txt`
- Worker strategy: up to 12 hardware threads, bounded work slices
- Correctness: exact integer aggregation and ordered deterministic output

The source is a restored earlier NewtonRedHeat implementation; historical best measurements are in `PERF.md`. It is not the separately evaluated experimental implementation.
