# Track C — Join

The Rust executable uses memory-mapped input, vector scans to locate CSV separators and quote spans, exact handling of embedded delimiters, a dense region-ID lookup and integer-cents arithmetic. Distinct names use full bytewise equality under hash collisions, with atomic per-country membership tracking. Thread-local aggregates are merged deterministically.

- Language: Rust, no external runtime dependencies
- Inputs: `/data/records.csv`, `/data/regions.csv`
- Output: `/out/aggregates.txt`
- Build: PGO training on independently generated input, fat LTO
- Correctness: exact CSV decoding, exact names and integer financial arithmetic

The selected source is the earlier NewtonRedHeat FALCON-C implementation. Historical measurements are in `PERF.md`.
