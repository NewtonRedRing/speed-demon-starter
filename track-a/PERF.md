# Track A: performance log

Every step from the reference to your final version, each with the live-board run ID that measured it. Judges check that the run IDs exist and that each change moved the way you say it did.

| Step | Change | Run ID | T (s) | M (MB) | Points |
|---|---|---|---|---|---|
| 0 | The reference, unchanged | | 32.05 | 228 | 0 |
| 1 | Original C++ submission, e8f4a84 | run-93a4bea20d70 | 0.628347 | 34.73 | 48.326 |

## November 1: claimed pieces and one shared endpoint table

The log is cut into 96 line-aligned pieces that threads claim in turn, instead
of one range per thread, so a slow core no longer delays the barrier. Endpoints
are interned in one lock-free table shared by all threads (a free slot is
claimed, filled and published), so ids are global while parsing: the serial
table merge and the per-record rank lookups are gone, and the hour phase
counts by id and walks the groups in name order. A first version that took a
lock when a thread met a name was much slower (about 34 ms), because the
threads all meet most names at once.

Local Linux-volume runs with the official timer, fresh sandboxed containers
and 16 interleaved runs per image: pieces alone 27.28 / 25.06 ms trimmed
(before / after), shared table on top 25.96 / 23.34 ms. Memory was within 1 MB.
Official full seed-1 hash, `check.py`, tiny seeds 2, 7, 42, 2026 and sample
seeds 17 and 103 all match the reference. Local timings are not board results.
The board run for this commit (`cae3c7b`) measured 30.783 ms, 19,308,544 B,
S=356 and 80.134076 points: no gain over the previous source (30.669 ms, 80.13
typical), so the local gain did not carry over to the board machine. The source
is kept because it is equal on the board and simpler in the serial step.

## November 1: read-ahead prefetch

One software prefetch of the log ahead of the line being parsed (the final source uses 2,048 bytes). Local runs saw only about 0.8 ms from it, but the board measured it: no prefetch 30.78 ms; 3,072 bytes 29.04 ms (80.738 points, run on a4aa542); 2,048 bytes 28.33 ms (80.946 points, a0fc00f, best); 1,024 bytes 32.8 ms and 8,192 bytes 31.1 ms (both on runs where the unchanged Track B was also 5-10% slower, so the ratio of A to B is the fair comparison: 0.1464 at 2,048, 0.1462-0.1471 at 3,072, 0.1515 at 8,192, 0.1546 at 1,024). A non-temporal hint (_MM_HINT_NTA) at 2,048 bytes scored lower (ratio 0.152). Static linking, sizing the group lists up front, and a second prefetch were also tried on the board and scored lower or equal. Full seed-1 hash and check.py pass.

## Local candidate measurements (not board runs)

| Candidate | Process median (s) | Peak RSS (MiB) | Repetitions | Validation |
|---|---:|---:|---:|---|
| Previous implementation | 0.541742 | 24.30 | 3 | Full public seed-1 hash |
| Parallel streaming, exact endpoint table, percentile selection | 0.104180 | 43.21 | 5 | Full public seed-1 hash on every run |
| Smaller sparse-group reservations; final candidate | 0.064004 | 40.90 | 5 | Full public seed-1 hash on every run |

Command: `python scripts\benchmark.py a --runs 5` from the repository root.
Tiny input, independent empty/randomized percentile oracles and a production-style
sandbox run also pass. Strict GCC warnings pass. Timings exclude Docker startup;
host load and hardware differ from the board, so cross-batch ratios are indicative,
not controlled comparisons. Final candidate is approximately 8.5x faster than
the recorded local baseline, with higher RSS. The oracle also passes ASan/UBSan.
90 points and first place remain unverified.
Latest official-input recheck: five runs, median 0.056666 s, peak RSS 40.58 MiB.
Inputs and outputs verified against the live manifest linked from the board.
The published candidate's actual board result is recorded below.

## Confirmed board result and subsequent local experiment

Commit `dfdc930`: run `run-fa8a1891ec46`, 0.078022 s, 52.05 MB,
271 formatted source lines, **66.618152 points**. Correctness passed.

A subsequent local small-group experiment sorts groups of at most 32 values
and retains selection for larger groups. Alternating nine-run measurements
gave baseline 0.082072 s / 40.73 MiB versus candidate 0.079945 s / 41.15 MiB.
The time/memory-only score delta is approximately +0.24 points, before the
small added-source penalty. This is a modest local candidate, not an official
score improvement or evidence of reaching 90 points.

Six-worker ingestion was rejected: its reduced memory did not offset its slower
time under the scoring exponents. The candidate retains twelve workers.

## Speed-first contiguous-storage redesign

The subsequent request prioritizes elapsed time. The retained local candidate
replaces per-group vectors with compact worker records, dense hour-major counts,
parallel prefix planning and disjoint scatter into one contiguous latency array.
Parsing uses a read-only mapping, direct next-record pointers and `memchr`
endpoint scans. Complete consumed pages are released in 1 MiB batches.
Endpoint remaps are reused rather than hashing every local endpoint a second
time. Output uses bounded direct buffers rather than repeated string appends.

The official full seed-1 file was measured, not inferred from typical cases:
3,100,000 rows, 1,842 endpoints, 126,908 populated groups, median group size six,
114,988 groups of at most 32 rows, maximum group size 9,223 and full-width
uint32 latencies. The old design created 145,601 worker-local vectors; simulating
its reserve-four/doubling policy gives another 176,342 growth allocations.
Only 362,667 latencies needed copying during cross-worker merge, so allocator
churn mattered more than that merge's 1.45 MB payload.

Temporary phase timers on the first contiguous candidate measured approximately
30 ms parsing, 10 ms serial layout planning, 4 ms scatter, 7-9 ms selection and
formatting, and 2-3 ms writing. Hour-major parallel planning reduced the planning
stage to about 4 ms; direct output formatting reduced its combined stage to
about 5-6 ms in later batches. Host load varies, and those phase ranges are not
a controlled comparison. All timing instrumentation was removed.

Final comparison used twelve runs of each executable, alternating execution
order in one sandbox against the same staged official inputs. Every run
produced exactly one output file and matched the live manifest's output hash:

| Executable | Median process time (s) | Maximum peak RSS (MiB) |
|---|---:|---:|
| Published implementation before redesign | 0.070378 | 41.01 |
| Retained speed-first candidate | 0.046220 | 68.10 |

This is **34.3% lower elapsed time (1.52x throughput)**, not an official score.
A separate seven-run final batch measured 0.043013 s and 67.20 MiB peak RSS.
Higher memory and source-size penalties must be included when judging points;
this request explicitly prioritized speed. No 80/90-point result is claimed.

Other tested alternatives:

- The initial single-scan buffered parser improved a controlled median from
  0.069867 s to 0.063940 s; initial contiguous scatter reached 0.057386 s.
- Endpoint-major parallel planning introduced unfavorable cache sharing;
  hour-major worker tables were retained.
- A 8192-slot endpoint table did not improve the controlled median over 4096.
- Fixed-method dispatch plus custom insertion sort did not beat the simpler
  method scan and standard small-group sort.
- Keeping the entire mapping resident used about 286 MiB without a reliable
  time advantage; consumed-page release was retained.
- Six/eight/twelve-worker controlled medians for the redesigned candidates were
  0.066952/0.056950/0.052070 s. Twelve workers remain.

Validation: seven final full official repetitions, twelve alternating full runs,
the allowlisted offline build and production-style sandbox all passed exact
hashes. All 14 regression tests pass. The independent A oracle now exercises all
1,842 catalogue entries, all seven methods, long endpoints, all 72 hours,
duplicates, uint32 extremes, and small/large percentile groups. Both A oracle
methods also pass ASan/UBSan; strict GCC warnings pass.

## Hardware-utilization follow-up: fused hour tasks

The previous candidate launched separate parsing, group-planning, scatter and
selection/formatting worker phases. The retained follow-up uses two phases:
parallel parsing into per-hour worker vectors, then dynamically scheduled hour
tasks that count, plan, scatter, select and format together. Hour slices are
disjoint, so tasks do not wait for other hours between those operations.
Worker counters now omit the unused offset field.

Sixteen alternating runs per executable in one sandbox, with all official
input/output hashes checked:

| Executable | Median time (s) | Maximum RSS (MiB) | Mean CPU-equivalents |
|---|---:|---:|---:|
| Previous four-phase candidate | 0.043447 | 68.32 | 7.41 |
| Fused hour pipeline, compact counters | 0.042055 | 66.59 | 7.72 |

This is a further **3.2% median time reduction** against the immediately
preceding candidate. CPU-equivalents are each run's user-plus-system CPU time
divided by elapsed time, averaged across runs; they are not a claim of twelve
continuously busy threads or the target CPU's utilization. A separate five-run
batch measured 0.042965 s and 66.39 MiB RSS.

One process uses twelve workers for full input; the 512-process and 8 GB limits
are capacity ceilings, not performance targets. Extra processes would compete
for the same twelve hardware threads. The local Docker host reports 28 CPUs and
about 2 GB VM memory; tests impose a twelve-CPU quota and the sandbox's 8 GB
limit, but cannot reproduce six P-cores, SMT placement or turbo-disabled clocks.
No official score or target-machine utilization is claimed.

All 14 regression tests, both A oracle methods under ASan/UBSan, strict GCC
warnings, and the full official-data production sandbox passed again.

## Profile-guided inline endpoint hashing

Current phase timers found 26-43 ms in mapped parsing, 2.2-2.5 ms reconciling
endpoints/layout, 7.8-8.7 ms in hour tasks and 2.5-3.3 ms writing output.
Timers were removed. The retained parser hashes endpoint bytes inline using
unaligned-safe `memcpy` word loads and a byte tail. Fingerprints still require
full string equality; the final catalogue uses the standard string-keyed map.

Eight alternating official-input runs per executable in one sandbox:

| Executable | Median process time (s) | Maximum RSS (MiB) |
|---|---:|---:|
| Start-of-pass implementation | 0.054506 | 66.92 |
| Inline endpoint hash | 0.052878 | 66.35 |

This is **3.0% lower median time**. Earlier independent controlled batches
showed roughly 1.1-4.2% reductions; this is a small, host-sensitive gain.
The separate final five-run median was 0.045975 s / 65.67 MiB.
All official hashes, all 14 tests, all eight solution methods under ASan/UBSan,
strict warnings and all four offline sandbox checks pass. The A oracle checks
every endpoint length from 1 to 125 and shared-prefix/shared-suffix names.
No new official score is available; formatted-source penalties still apply.

## Experimental compact pipeline and fused parsing

Developed on `experiment/track-a-speed-first`, based on submitted commit `60e1b80`,
then promoted to the submission branch `speed-demon` after validation.
These are local experiments, not live-board runs; no official run ID or
90-point result is available.

### Measurement correction

Each measured process runs in a fresh cgroup with 12 CPU quota, 8 GB/no swap,
512 PIDs, no network, read-only root/input, unprivileged user and 1 GiB scratch.
A separate live staging cgroup reads the entire official input before every
run. A small static supervisor measures fork/exec-to-exit wall time, child CPU
and RSS, then reads its cgroup's `memory.peak`. The cgroup peak includes that
supervisor, output cache and kernel charges. It is not `ru_maxrss`.
Output filenames and SHA-256 are checked outside the timed interval, every run.
Warmups are excluded; executable order alternates and reverses between rounds.
Inputs and expected outputs are verified against the live official manifest.

This better models externally warmed page-cache accounting than the previous
RSS-only reports. It does not reproduce the official scoring box, six physical
P-cores, CPU affinity, disabled turbo, or the official five-run trimmed mean.
Host load still changes timings. Source size must still be measured after the
organisers' formatter; no official K is inferred from these local medians.

### Retained changes

- Pack each record into six bytes, preserving full uint32 latency, eleven
  endpoint bits and one error bit. Remove parser-time hour/group counter
  updates and reconstruct counts in the claimed hour's compact table.
- Replace the dataset-sized secondary latency allocation with hour-local
  arrays. Source vectors are freed immediately after their hour is scattered.
- Fuse bounded endpoint delimiter scanning and CRC32C hashing. Hash table hits
  always compare the entire endpoint, so CRC collisions cannot merge groups.
- Use a shared SWAR delimiter mask for methods, endpoints and latencies.
  Decimal pairs/four-digit halves parse up to eight digits at once; scalar
  tails handle longer values and short final records.
- Select exact percentiles directly for one/two values, with an unsigned AVX2
  network for three/eight, `std::sort` for nine/32 and ordered `nth_element`
  above 32. Masked vector accesses never read/write a neighboring group.
- Right-size output by present groups and endpoint lengths, including numeric
  field bounds and `to_chars`' writable range.
- Increase initial hour-vector reservations moderately. This reduced cgroup
  peak without a measurable time difference in its isolated comparison.

Latest eight measured repetitions per executable:

| Executable | Median process time (s) | Median cgroup peak (MiB) | Median child peak RSS (MiB) |
|---|---:|---:|---:|
| Submitted `60e1b80` implementation | 0.05048445 | 69.53 | 64.51 |
| Retained experimental implementation | 0.04034590 | 36.39 | 33.16 |

This is **20.1% lower median runtime**, **47.7% lower median cgroup peak**,
and 23% less median child CPU time. Earlier complete comparisons measured
0.06097745 -> 0.04922250 s and 0.07266175 -> 0.05171145 s, reductions of
19.3% and 28.8%. These are separate batches, not cumulative percentages.
All repetitions matched the official full seed-1 hash.

### Individual experiments

Each row is a separate comparison against its immediate baseline, not a
comparison between timings on different rows.

| Experiment | Before / candidate median (s) | Decision |
|---|---|---|
| Packed records, deferred counts, hour-local latencies | 0.04901685 / 0.04503785 | Retain |
| Remove page-discard calls, first test | 0.04988105 / 0.04932015 | Inconclusive |
| Fused SWAR/CRC endpoint scan | 0.04563525 / 0.04340485 | Retain |
| Bounded chronological streaming writer | 0.03850400 / 0.04055525 | Reject: waits slow workers despite lower memory |
| Right-sized output without writer waits | 0.04444265 / 0.04301715 | Retain |
| Eight-lane AVX2 small-group sorting | 0.03893415 / 0.03679235 | Retain |
| Extend network to sixteen lanes | 0.03703640 / 0.03793225 | Reject |
| PGO on official generator sample seeds 349/719 | 0.04559000 / 0.04540945 | Reject: no reliable gain |
| Six / eight / twelve workers | 0.07247620 / 0.05617310 / 0.04462865 | Keep twelve |
| Direct singleton/pair formulas | 0.04286825 / 0.04260680 | Keep exact simple special cases; no isolated speed claim |
| Reserve a shard-sized vector for each hour | 0.03996245 / 0.04248815 | Reject: slower |
| Moderate hour-vector reservation increase | 0.04433575 / 0.04441350 | Neutral time, approximately 10.5% lower cgroup peak |
| SWAR method delimiter | 0.04401195 / 0.04154160 | Retain |
| SWAR decimal latency parsing | 0.05831835 / 0.05651125 | Retain |

Repeated removal of page-discard calls did not establish a speed win.
In the final combined three-way test, the resident version took 0.05386590 s
versus 0.05171145 s with page release; cgroup peaks were 36.58 and 36.89 MiB,
while RSS was 250.24 and 32.86 MiB. Keep page release, but do not mistake the
RSS difference for an equivalent scoring-memory saving.

PGO used a byte-identical official generator and an offline Python build stage,
with atomic instrumentation counters and strict missing/mismatched-profile
errors. Neither training inputs nor reference answers entered the runtime
image. The unused generator and PGO build stages were removed after rejection.
In-place American-flag record partitioning, two-line interleaving and radix
multiselect were not implemented or benchmarked in this pass. Hour-local
scatter already removes the dataset-sized secondary array; further changes
need evidence, not assumed wins.

### Correctness and build checks

- All 15 software tests pass, including seeded ascending/descending/equal/
  extreme/random groups, group sizes 1-72/513/4097, all decimal lengths and
  full uint32 values.
- All three A oracle methods pass ASan/UBSan, compiled for `-march=haswell`
  (AVX2, without AVX-512), with strict warnings treated as errors.
- Additional official-generator sample seeds 23 and 9871 match the original
  Python reference byte for byte, twice each. These are correctness fixtures,
  not performance or official-score evidence.
- The pinned offline production image passes the sandbox and the manifest's
  full seed-1 SHA-256. There are no added runtime dependencies or build tools.

## 2026-10-31: Scoring review and Track A portability patch

Reviewed the current published rules against a proposed source-size/build.rs
rewrite. The effective formula is
`K=(T_ref/T)^0.50*(M_ref/M)^0.20*(S_ref/S)^0.15*(D_ref/D)^0.15`,
with source and dependency advantages capped at 1× and memory capped at 50×;
there is no scaling X axis, and runtime speed is uncapped. For Track A the
published reference is 32.05 s / 228 MB / 56 scc code lines. The proposed
claim that hiding source in an `.inc` file guarantees 90 points was wrong.
We retained the readable `main.rs` and digest-pinned, offline, PGO-enabled
Dockerfile rather than replace them with an unpinned build.

Implementation change: bound the worker count by the available CPU set and
12 scoring threads. Use an 8,192-slot CRC32C interner table with explicit
power-of-two wrapping to reduce probes while retaining the exact bytewise
collision check. Keep the independent 11-bit packed local ID limit (2,048):
increasing table slots alone does not increase the representable ID range.
The official generator has 1,842 distinct endpoint templates.

This is a code review and engineering change, **not** a claimed performance
improvement. No new local benchmark or seed-1 hash was run in this environment;
the live board must independently validate the pushed commit. No run ID or
score is fabricated.

## 2026-10-31: Paired-decimal formatting

Replaced per-digit decimal emission with a compile-time 100-pair lookup to
reduce division work in the output stage. A local Python equivalent passed
200,014 integer cases, including 0 and u32::MAX. Rust build, production hash,
local speed and official scoring are not verified in this session.

### 2026-10-31: Reject paired-decimal writer and later PGO build

The live board run `run-3b8f1a6bdd06` at commit `39502cdb` scored
79.269335 with T=0.030782 s, M=22,573,056 B, S=377. The prior best
`run-5fbf4e3e8868` scored 79.686448 with T=0.030690 s,
M=21,938,176 B, S=348. This is a comparison of two full submissions,
not a controlled attribution; nonetheless the new larger writer and PGO
pipeline showed no measured speed improvement. Revert to the simpler writer
and the pinned baseline build, retaining CPU-set and endpoint fixes.

## 2026-10-31: Track A proposed improvements (unmeasured candidate)

Applied: M1 (8 KiB chunks), T1 (MADV_SEQUENTIAL input map), T5 (reuse
AVX2 vector loads for space and newline comparisons), T6 (branchless
Oct/Nov day offset), T7 (sequential prefetch), T11 (one-/two-digit
latency fast path), T21 (reuse per-worker cursor), and T23 (exact n=1/2
percentile shortcuts). Existing runtime CPU-set discovery and offline,
pinned build were retained. One-line source obfuscation S1 is not effective
under official post-rustfmt scc accounting; S2/S4 are not appropriate for
human readability and have not been applied. The changes remain unbenchmarked
until an official output hash and board runtime are available.

### 2026-10-31: Exact CRC-tagged interner and CPU affinity (experimental)

T12 stores the high 16 CRC32C bits beside each nonzero 1-based endpoint ID,
rejecting most probe mismatches without reading endpoint bytes. T16 adds a
64-entry direct-mapped recent-ID cache; even a 32-bit CRC collision must pass
full byte equality before returning an ID. A local Python model passed 100,000
inserts/lookups across 1,800 distinct names, including repeated hits and
probe collisions. T26 maps each spawned worker to the corresponding CPU in
the sandbox's **allowed** affinity mask, with a safe no-op fallback when the
syscalls are blocked. These have not yet been benchmarked on the scorer.

### 2026-10-31: SWAR latency decoder (T10)

The parser now decodes one-to-eight decimal digits by masking bytes preceding
the number, subtracting ASCII '0' and combining pairs/quadruples/eight-digit
words with 64-bit arithmetic. The existing SSE routine is retained for 9- and
10-digit latencies. An independent 100,000-case randomized Python model
matched decimal parsing for lengths 1..8. A local Clang C microbenchmark
suggested SWAR faster than the prior SSE path, but its small fixed input is
not representative of full-process wall time. This optimization still needs
Rust compilation and official correctness/time validation.

## 2026-10-31: Fused aggregation / exact radix / AVX2 sort experiment

**Current change, not an official measurement.** The previous latest scored
Track A run was `run-c42954603af9` at `a2c6f51d`: 0.046989 s,
22,585,344 B, 431 scc code lines, 74.238468 points (correctness passed).
The best earlier run was `run-5fbf4e3e8868`: 0.030690 s,
21,938,176 B, 348 lines, 79.686448 points.

This patch removes explicit 192-byte T0 software prefetch, CRC-tagged u32
slots plus the hot hash cache, affinity pinning and the unproven SWAR path.
The 4,096-entry u16 interner, 16 KiB chunk size, single SSE decoder and
baseline image were restored. The old board regression bundled multiple
changes, so its **cause cannot be isolated** to any one micro-optimization.

### Algorithmic changes

1. For each hour, count/error and append a packed 64-bit
   `(global_rank << 33)|(error << 32)|latency` record in one pass
   through the mmap chunks, including wide latencies. Prefix and scatter
   using the contiguous worker-local buffer. This removes the second
   linked-list traversal, at the cost of a transient 8-byte-per-row staging
   buffer (roughly 344 KiB for a typical 43k-record hour).
2. Exact three-target, four-round MSB-first radix histogram selection for
   at least 256 records. Three 256-bin histograms are updated in one scan
   at each byte level. Duplicate values and full `u32` range are exact.
3. Exact AVX2 bitonic sorting network, padded with `u32::MAX` to the next
   power-of-two vector width up to 32. Enabled only for 28..32 values;
   small groups still use ordinary `sort_unstable`.
4. Original chained `select_nth_unstable` retained for 65..255 values.
   Zero to two records use previously validated exact fast paths.
5. Optional pinned PGO build under `scripts/track-a-pgo.Dockerfile`, with
   sample and tiny edge-case training, **not** in the submitted Dockerfile.

AVX2 lacks the AVX-512CD `VPCONFLICT` instruction and vector scatter.
The proposed conflict-detecting eight-lane histogram is therefore not
available on the scoring ISA; its exact scalar counting path remains.

### Independent local algorithm probes (NOT board or Rust measurements)

- Python order-statistic model: randomized nearest ranks over hundreds of
  lengths, repeated small values and 32-bit extremes.
- Equivalent compiled AVX2 intrinsic sorting network: 500,000 randomized
  cases for sizes 8..32, all compared against `std::sort`.
- Proxy C++ selection timing, ratio radix / chained select:
  65=1.33x, 128=1.12x, 256=0.93x, 512=0.86x,
  1024=0.89x, 9223=0.87x.
- Proxy C++ small-group timing, ratio bitonic / `std::sort`:
  8=2.74x, 10=3.45x, 16=1.37x, 24=1.02x, 32=0.68x.
  These tests were on a different CPU/compiler and **do not establish**
  the crossover points for Rust on the official i5-13500.

### Required verification before claiming an improvement

```bash
rustc --edition=2021 -C target-cpu=native --test tests/track_a_algorithms.rs \
  -o /tmp/track-a-tests && /tmp/track-a-tests
python3 check.py a            # original public full seed-1, 5 exact SHA gates

# Optionally compare with independently built PGO:
docker build --network=none -t speeddemon-pgo/track-a \
  -f scripts/track-a-pgo.Dockerfile track-a
python3 check.py a --skip-build --image speeddemon-pgo/track-a
```

The extra scored lines and transient memory can outweigh small speed wins.
Only an actual official run can establish the new score; no low-90s claim is
supported by this implementation alone.


## 2026-11-01: restore best source; isolate incremental experiments

The 2026-10-31 official board snapshot lists the best Track A run
`run-5fbf4e3e8868` at commit `73169015`:
**79.686448 points, 30.690 ms, 21,938,176 B, 348 scc lines**.
It lists the latest full fused/radix run at `8aac5a80`:
**76.723315 points, 32.614 ms, 29,110,272 B, 484 scc lines**.
Both passed the correctness gate.

As requested, restored the exact best-scoring `track-a/main.rs` bytes on
the scored `speed-demon` branch (Git blob SHA
`a16b72dd8ef8c05c44431e7ee15daa6122684b09`). Other tracks and the
new mandatory original-full-data verification workflow are unchanged.
Preserved the previous fused/radix source separately as
`experiment/track-a-fused-radix`.

The active minimal `experiment/track-a-small-groups` branch tests two
individually attributable source changes:
- `5bf7f3b9`: skip generic unstable sorting for n=1 and n=2, preserving
  exact nearest-rank values (p50 is min for n=2, p95/p99 are max).
- `89dae5fb`: allocate the endpoint scatter cursor once per worker instead
  of once per hour.

For these changes, an independent 100,000-case mathematical n=1/2 Python
check passed. That does **not** imply the Rust candidate compiled or scored.
Use `python3 scripts/compare_track_a.py` to build both revisions under
the same pinned offline Track A Dockerfile, enforce the real public
seed-1/full input and published output hashes, and compare child-only time
and observed cgroup peak. The benchmark wrapper is root-level and cannot
alter the scored track's source-size or dependency axes.

No new score above 79.686448 is claimed. No experiment is promoted until
independently verified against the organizer's full dataset and shown to
improve the complete machine-score tradeoff on the target CPU.

## 2026-11-01: Experiment #1 — file-mapping prefault

- Branch: `experiment/track-a-prefault`; candidate commit `5560b3f4`.
- Baseline: byte-for-byte original highest-scoring Track A source at
  `73169015` / Git blob `a16b72dd8ef8c05c44431e7ee15daa6122684b09`.
- **Only scored-code change:** `assert_eq!(madvise(p.cast(), size, 22), 0)`
  after `mmap` of the read-only input file, with 22 = `MADV_POPULATE_READ`.
- The syscall operates inside the timed process. It can relocate page faults,
  not make them free. Linux 5.14+ is required; fail if unsupported rather
  than silently accepting a no-op.
- Local diagnostics added to `scripts/track_a_timer.rs` measure Linux child
  minor/major faults with `getrusage(RUSAGE_CHILDREN)`. The A/B helper adds
  an `--interleave` mode (alternating baseline/candidate within each pair),
  and `--reverse` to reverse the first pair's order. The scripts do not
  contribute to the submitted `track-a` source-size metric.
- At the time this experiment was first documented, it had not yet been built
  or measured and was not merged into `speed-demon`. A subsequent isolated
  full-input `MADV_POPULATE_READ` comparison found no supported gain; see the
  later investigation section below.
- Decision criteria: full public input/output hashes must match for all runs;
  compare trimmed process times, maximum memory and formatted source count.
  Total minor faults alone do not prove time savings.

## 2026-11-01: isolated harness and hot-loop investigation

The current working candidate differs from the restored best source by one
line: `drop((start, errors, latency));` before final output planning. This
releases worker-local aggregation scratch before formatting. It is not the
historical fused/radix candidate described above.

The organizer's setup places `/out` on host tmpfs. The final confirmation
therefore used tmpfs output, the unchanged offline production Dockerfile,
the original complete public seed-1 input, and exact output SHA-256 on every
run. The expected output is removed before every run and unexpected entries
fail the gate. Nine adjacent pairs alternated AB/BA order after excluded warmup.
MB below is decimal; trimmed time removes the fastest and slowest observations
from each nine-observation sample, and is not an official five-run statistic.

| Candidate | Local trimmed T | Maximum observed cgroup peak | Paired T ratio, bootstrap 95% interval |
|---|---:|---:|---|
| Pinned baseline | 37.688 ms | 23.843 MB | 1 |
| Release worker scratch, retained | 35.615 ms | 21.406 MB | 0.9953 [0.8249, 1.0610] |

This batch's median paired peak ratio was 0.8941. Its complete local K ratio
was 1.0254 with interval 0.9924..1.1111, crossing no improvement.
**Only the memory reduction is repeatable;
a time or score improvement is not established.** Scratch release remains a
small memory-reduction candidate pending target-machine comparison, not a
claimed score win. Earlier batches changed direction on timing.

The final report is `.out/four-track-fresh-output.json`. Earlier reports
reused the output inode between runs, allowing stale-file correctness checks
and potentially inherited page charges. Their memory/score estimates are
exploratory only and are superseded by this fresh-output rerun.

Canonical source accounting includes the Dockerfile: 348 baseline lines,
349 candidate lines. Some earlier exploratory JSON counted only Rust; those
counts must not be used as official S. Initial disk-backed output experiments
are also explicitly preliminary, not evidence about the organizer's tmpfs.

### Disposition of proposed changes

| Idea | Evidence and decision |
|---|---|
| Input prefaulting | Serial mmap/touch diagnostic: baseline 7.862 ms, 3,648 minor faults; populate 7.422 ms, same faults. Whole-process preliminary medians were 46.427/47.020/50.041 ms for baseline/MADV_POPULATE_READ/MAP_POPULATE. No supported gain; unchanged. The touch diagnostic does not isolate fault cost. |
| Read-only file THP | Kernel configuration disables `CONFIG_READ_ONLY_THP_FOR_FS`. All mapping probes showed 4 KiB pages, `FilePmdMapped: 0`, `THPeligible: 0`; advice did not create huge pages. Cannot extrapolate to the scoring kernel. |
| Chunk-only THP disabling | Tested as an isolated variant; no reliable local advantage. Process-wide setting unchanged. |
| Startup flags | Fat LTO, panic-abort, one codegen unit and native CPU were already enabled by direct rustc. Isolated strip/static-link/fstat changes did not justify promotion. |
| One load, two comparisons | Optimized assembly already shares the offset-28 and offset-60 vector loads between comparisons. Explicit source rewriting did not remove loads. |
| Eight-way bucket interner | Fifteen preliminary disk-output pairs produced paired T ratio 1.0735; no supported gain. Not retained. |
| 64-entry hot cache | Fifteen preliminary pairs: T ratio 1.1870, interval 1.1411..1.3131. Rejected locally. |
| Parser-time counters | Must count per hour AND endpoint, not one 8 KiB table. Lazy hour-aware arrays increased peak memory; time/score benefit inconclusive. Not retained. |
| Fixed hour labels | Stack and constant variants were tested independently; no repeatable whole-score benefit. Not retained. |
| Output fallocate | No supported gain. The final harness uses tmpfs, not disk block allocation. Not retained. |
| Six versus twelve workers | Fifteen preliminary pairs favored twelve for time; lower six-worker memory did not establish a score gain. Not retained. |
| Heap trimming/summary mmap thresholds | Tmpfs retests did not justify extra code over plain scratch release. Not retained. |
| Final chunk release | No supported improvement beyond the existing hour-task reclamation. Not retained. |

Not implemented or established by this investigation: non-temporal output,
producer/consumer SMT split, explicit sibling affinity, backward parsing,
two-line SIMD, libc scan replacement, branchless wide latency, software
pipelining, new small-group networks, compressed records, or a 56-line rewrite.
They remain hypotheses, not claimed gains. The current representation packs
latency plus endpoint/error information in four-byte records; it is not simply
a standalone 12.4 MB latency array that can be replaced by three-byte values.

The VM is an i7-13850HX with unverified P-core/SMT topology, not the scoring
i5-13500. `perf_event_open` is denied, so cycles, instructions, cache misses and
branch misses were not measured. No official target-machine fault-cost or
topology conclusion is possible here. Observed local cgroup peaks are not the
organizer's post-removal parent-cgroup accounting.

Reproduction (repository root):

```powershell
python scripts\investigate_tracks.py --tracks a --runs 9 --variants baseline release-scratch --output .out\a-scratch.json
python scripts\investigate_tracks.py --tracks a --runs 15 --variants baseline release-scratch --output .out\a-confirmation.json
python scripts\investigate_track_a.py --help
python -m unittest discover -s tests -p test_track_a_probe.py -v
python check.py all --runs 5
```

Both experiment runners default to immutable baseline
`7a9886d0635e3deb74493f2c23cde76c30c521c2`; use `--base-ref` deliberately
for another revision. Variants fail when expected source anchors are absent.
The A-specific runner also provides fault/mapping probes and parse-only mode.
Its final smoke builds passed all five Rust regression checks for both variants.
Raw local reports are under ignored `.out`; no benchmark code enters the track
build context. Five final production A checks and all twenty four-track
correctness checks passed. The 16 ms/18 MB target remains unverified.
