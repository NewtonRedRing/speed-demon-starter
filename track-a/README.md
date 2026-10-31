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

Board comparison (run `run-3b8f1a6bdd06`) showed that pair-digit output
plus the later build pipeline did not beat the earlier best A result. The
writer was restored to simple single-digit emission and the fast baseline
pinned offline Rust build. This is a measured rejection, not a missing feature.

## 2026-10-31 candidate optimizations

The parser now reuses AVX2 byte-classification loads, prefetches the input,
and handles short latencies without vector decimal decoding. Hours reuse
scatter cursor storage and shortcut single-/two-record percentile groups.
Records are stored in 8 KiB mmap chunks, and the input mapping receives
MADV_SEQUENTIAL. These are correctness-preserving algorithmic changes on paper;
a new full-output hash and board time/memory measurement are required before
calling them improvements. The Rust remains readable, not minified.


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
| M1 | Applied; unmeasured | 8 KiB record chunks, adjusted release threshold |
| M2 | Not used | DONTNEED on the log risks refaulting names still used during render |
| M3 | Not used | 72 tiny empty `Vec` headers; flattening adds hours of filtering |
| T1 | Applied; unmeasured | MADV_SEQUENTIAL on the input mapping |
| T2 | Not used | Process-wide THP is disabled; file THP support must be measured |
| T3 | Skipped | Input already pre-warmed; no expected benefit |
| T4 | Not used | Selective THP requires an independently verified peak-memory tradeoff |
| T5 | Applied; unmeasured | Two AVX2 loads for both spaces and newlines |
| T6 | Applied; unmeasured | October/November day correction uses multiplication |
| T7 | Applied; unmeasured | Sequential cache-line prefetch |
| T8 | Deferred | Manual 2–4-record batching needs an instruction-level benchmark |
| T9 | Deferred | Only unusual long endpoints take the extra scan path |
| T10 | Applied; unmeasured | 64-bit SWAR decode for 1..8 digits, SSE for 9..10 |
| T11 | Applied via T10 | Short digit counts avoid the SIMD decoder |
| T12 | Applied; unmeasured | CRC16 high tag in every nonempty lookup slot |
| T13 | Skipped | Slot fits cache; added prefetch on every hot-cache hit is costly |
| T14 | Skipped | Low expected benefit at current hash-table occupancy |
| T15 | Deferred | Eight-wide SIMD probing may be slower on first-hit/empty probes |
| T16 | Applied; unmeasured | Exact 64-entry direct-mapped endpoint cache |
| T17 | Rejected earlier | Paired-digit output regressed on a recorded board run |
| T18 | Deferred | Branchless width comparisons not benchmarked vs `checked_ilog10` |
| T19 | Rejected for now | Up-front latency reserve increases peak memory |
| T20 | Skipped | Name length is accessed once per emitted group |
| T21 | Applied; unmeasured | Reuses per-worker counting-sort cursor |
| T22 | Deferred | One-pass scatter requires a separately validated representation |
| T23 | Applied; unmeasured | Exact 1-/2-record nearest-rank percentiles |
| T24 | Deferred | AVX2 bitonic 8..32 sort requires representative timing |
| T25 | Deferred | Multi-radix percentile selection for rare large groups |
| T26 | Applied; unmeasured | Workers pin only within allowed CPU mask, fail open |
| T27 | Deferred | Removing barriers risks overlapping stage dependencies |
| T28 | Tested previously | Historical 6/8/12-thread runs preferred 12 |
| T29 | Already present | Pinned Docker build uses target-cpu=native |
| T30 | Rejected previously | Track A PGO lost in recorded board comparison |
| T31 | Not applicable | PGO diagnostics only, no PGO in current A Dockerfile |
| C1 | Covered by spec | 1,842 endpoints; packed local-ID limit is 2,048 |
| C2 | Other track | Name hash exactness issue belongs to Track C |
| C3 | Already present | Barriers serialize hour offsets and output rendering |

**Validation status:** The changes above require a fresh Rust compile, full
seed-1 SHA256 output check, and wall-time/cgroup peak-memory comparison. None
of the proposed +point effects is stated as an achieved result.
