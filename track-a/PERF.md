# Track A: performance log

Every step from the reference to your final version, each with the live-board run ID that measured it. Judges check that the run IDs exist and that each change moved the way you say it did.

| Step | Change | Run ID | T (s) | M (MB) | Points |
|---|---|---|---|---|---|
| 0 | The reference, unchanged | | 32.05 | 228 | 0 |
| 1 | Original C++ submission, e8f4a84 | run-93a4bea20d70 | 0.628347 | 34.73 | 48.326 |

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
