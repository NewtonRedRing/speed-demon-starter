# NewtonRedHeat — Speed Demon 2026

Four independent, zero-runtime-dependency Rust submissions, built from the `speed-demon` branch.

| Track | Workload | Selected implementation |
|---|---|---|
| A · Ingest | HTTP log aggregation | Parallel AVX2 parsing, packed 4-byte records, exact percentiles |
| B · Index | Boolean and phrase queries | Query-first selective indexing, sparse postings, bitmaps and PGO |
| C · Join | Hostile CSV aggregation | SIMD CSV parsing, exact bytewise name equality, atomic country bitsets and PGO |
| D · Traverse | Weighted directed shortest paths | Packed CSR, BFS-locality renumbering, bidirectional Dijkstra with radix heaps |

Each Dockerfile has a digest-pinned, offline Rust build. The executable reads `/data`, writes the track's specified single file to `/out`, and has no external runtime crates.

## Historical machine-score evidence

| Track | Previously recorded machine points | Runtime | Run |
|---|---:|---:|---|
| A | 84.279208 | 21.662 ms | `run-d45aa4ea3d05` |
| B | 72.172141 | 183.883 ms | `run-f979478c0277` |
| C | 74.320728 | 36.405 ms | `run-6dcd8ad2012f` |
| D | 68.316671 | see track log | previously recorded radix implementation |

These are historical practice results, not a claim that this cleanup commit has been freshly scored. A higher experimental run on another source does not establish the score of these selected versions. The judged holdout remains unverified.

## Verification

From the repository root:

```sh
python3 check.py a --data /path/to/a --expected <known-reference-sha256>
python3 check.py b --data /path/to/b --expected <known-reference-sha256>
python3 check.py c --data /path/to/c --expected <known-reference-sha256>
python3 check.py d --data /path/to/d --expected <known-reference-sha256>
```

The local checker validates builds, sandbox restrictions and output digests. Compare on full-size and independent generated datasets before claiming a new benchmark. `track-*/PERF.md` records measurement provenance.

## Licensing

`LICENSE` is retained for the project and any material governed by its terms. This source snapshot restores earlier NewtonRedHeat engineering versions rather than the subsequently imported competitor implementation files. Git history remains available for technical traceability.
