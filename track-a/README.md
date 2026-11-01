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

## Current candidate: isolated scratch release

The source is the restored 348-line best-scoring baseline plus explicit release
of each worker's count, error and latency scratch before output planning.
Canonical Rust plus Dockerfile accounting is now 349 lines. Historical
fused/radix sections below describe experiments, not this implementation.

This is retained as a memory-reduction candidate, not an established score win.
Timing changed direction across batches. The final nine-pair production-build
comparison, with fresh tmpfs output on every run, found time ratio 0.9953
(bootstrap 95% interval 0.8249..1.0610), with median paired peak reduction of
10.6%. The complete-score interval still crosses no change.
These are local VM measurements, not official results or the 16 ms/18 MB target.
All five final production-build official-input/output checks passed.

Reproduce the isolated comparison from the repository root:

```powershell
python scripts\investigate_tracks.py --tracks a --runs 9 --variants baseline release-scratch --output .out\a-scratch.json
```

The runner uses immutable baseline commit
`7a9886d0635e3deb74493f2c23cde76c30c521c2`, not the edited working tree.
It preserves the production Dockerfile, warms input outside the measured
container, defaults to tmpfs output, and checks every output SHA-256.
See `PERF.md` for the other tested ideas and local-harness limitations.

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

The official digest-pinned, non-PGO Rust Docker build is preserved. No source-hiding `build.rs` or `.inc` indirection is used: the
2026-10-28 scoring rules cap the source-size benefit at 1×, and the
`scc` count covers all source under this track's build context.

Board comparison (run `run-3b8f1a6bdd06`) showed that pair-digit output
plus the later build pipeline did not beat the earlier best A result. The
writer was restored to simple single-digit emission and the fast baseline
pinned offline Rust build. This is a measured rejection, not a missing feature.

## 2026-10-31: Fused aggregation and exact percentile experiments

The previous regressing candidate's explicit 192-byte software prefetch,
32 KiB CRC-tagged endpoint table, 64-entry cache and worker affinity
are removed. The compact 4,096-slot (16 KiB) exact byte-checked interner,
16 KiB record chunks, original SSE decimal decoding and the official
digest-pinned non-PGO Dockerfile are restored.

Each hour now visits the mmap chunk chains **once** to count errors and
stage packed (global endpoint rank, error flag, latency) words in a
worker-local contiguous buffer. Counting-sort scatter reads only that flat
buffer. Exact percentile selection uses small-group sorting and adaptive
strategies: AVX2 bitonic network for 28..32 values, classic chained
selection for 65..255, and three-way shared-histogram radix selection
for >=256. No approximate KLL or t-digest sketches are used.

Why adaptive? Local, **non-official** C++ proxy benchmarks found the SIMD
network slower than comparison sorting for small groups and radix selection
slower than chained selection for groups around 65..128. Those costs must be
rechecked with the actual Rust build on the scoring CPU. Root-level
`tests/track_a_algorithms.rs` provides randomized exactness checks.

AVX2 has no hardware conflict-detection scatter instruction
(`VPCONFLICT` belongs to AVX-512CD), so the endpoint histogram stays
scalar. An edge-case-and-sample PGO build is provided as an **optional**
benchmark in `scripts/track-a-pgo.Dockerfile`; it is not silently
substituted into the scored Dockerfile.

**Not yet verified:** Rust compilation, original full seed-1 SHA-256
output, cgroup memory peak, or a higher board score for this revision.
See `PERF.md` for the test methodology and prior official run IDs.

## Track A attachment checklist — 2026-10-31

The optimization menu in the request contains alternative implementations and
some explicitly rejected ideas. **Applied** means present in `speed-demon`;
it does not mean the scorer has established a speed improvement. The official
source counter runs on formatter-normalized Rust, so single-line source tricks
are not a valid basis for a projected six-point gain.

| Option | Status | Reason / implementation |
|---|---|---|
| S1 | Not used | Canonical rustfmt restores physical lines before `scc` |
| S2 | Not used | Dense 56-line code would sacrifice maintainability and be reformatted |
| S3 | Not used | `scc` already omits Rust comments |
| S4 | Not used | Build-time source hiding counts toward the source-size axis |
| M1 | Reverted | 16 KiB chunks restored after bundled micro-optimization regression |
| M2 | Not used | DONTNEED on the log risks refaulting names still used during render |
| M3 | Not used | 72 tiny empty `Vec` headers; flattening adds hours of filtering |
| T1 | Reverted | Baseline mapping restored until isolated measurement |
| T2 | Not used | Process-wide THP is disabled; file THP support must be measured |
| T3 | Skipped | Input already pre-warmed; no expected benefit |
| T4 | Not used | Selective THP requires an independently verified peak-memory tradeoff |
| T5 | Applied; unmeasured | Two AVX2 loads for both spaces and newlines |
| T6 | Applied; unmeasured | October/November day correction uses multiplication |
| T7 | Reverted | Removed explicit software prefetch from the sequential parser |
| T8 | Deferred | Manual 2–4-record batching needs an instruction-level benchmark |
| T9 | Deferred | Only unusual long endpoints take the extra scan path |
| T10 | Reverted | Restored the original SSE decoder |
| T11 | Reverted | SSE handles 1..10 digits again |
| T12 | Reverted | Compact 16-bit endpoint slots restored |
| T13 | Skipped | Slot fits cache; added prefetch on every hot-cache hit is costly |
| T14 | Skipped | Low expected benefit at current hash-table occupancy |
| T15 | Deferred | Eight-wide SIMD probing may be slower on first-hit/empty probes |
| T16 | Reverted | Removed cache and associated hot-path overhead |
| T17 | Rejected earlier | Paired-digit output regressed on a recorded board run |
| T18 | Deferred | Branchless width comparisons not benchmarked vs `checked_ilog10` |
| T19 | Rejected for now | Up-front latency reserve increases peak memory |
| T20 | Skipped | Name length is accessed once per emitted group |
| T21 | Retained | Reuses per-worker counting-sort cursor |
| T22 | Applied; unverified | One chain walk then a contiguous flat-buffer scatter per hour |
| T23 | Applied; unmeasured | Exact 1-/2-record nearest-rank percentiles |
| T24 | Experimental | Bitonic sorting network implemented; enabled for 28..32 groups only |
| T25 | Experimental | Exact shared-histogram radix selected for groups of at least 256 |
| T26 | Reverted | OS scheduling restored |
| T27 | Deferred | Removing barriers risks overlapping stage dependencies |
| T28 | Tested previously | Historical 6/8/12-thread runs preferred 12 |
| T29 | Already present | Pinned Docker build uses target-cpu=native |
| T30 | Optional experiment | Edge-case PGO Dockerfile exists outside scored track |
| T31 | Not applicable | PGO diagnostics only, no PGO in current A Dockerfile |
| C1 | Covered by spec | 1,842 endpoints; packed local-ID limit is 2,048 |
| C2 | Other track | Name hash exactness issue belongs to Track C |
| C3 | Already present | Barriers serialize hour offsets and output rendering |

**Validation status:** The changes above require a fresh Rust compile, full
seed-1 SHA256 output check, and wall-time/cgroup peak-memory comparison. None
of the proposed +point effects is stated as an achieved result.
