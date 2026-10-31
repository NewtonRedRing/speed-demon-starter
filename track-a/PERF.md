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


## October 31 speed-first experiment: deferred endpoint counts (not submitted)

The isolated branch `experiment/track-a-speed-first-deferred-count` removes per-worker dense hour-by-endpoint
counter arrays and packs the error flag into bit 11 of each 32-bit local
record key. An hour worker computes exact counts and 5xx counts by traversing
its compact hour records, then plans/scatters latencies and selects the same
nearest-rank percentiles as the baseline. This trades sequential extra reads
for fewer random counter writes in the hot parser.

Two binaries were compiled from the exact Git blobs of the baseline and new
candidate using GCC C++20, `-O3 -DNDEBUG -flto -march=native -pthread`.
The independent generated benchmark contained 3,100,000 well-formed records
and 244,196,814 bytes; it is **not** the official seed-1 workload or target
machine. Eight alternating process-run rounds found medians of **135.50 ms**
(old) and **125.14 ms** (candidate): **1.083x speedup**. A second nine-round
batch found 121.00 ms and 109.92 ms. Host load and cache conditions varied.
Both binaries emitted identical SHA-256 for every full-size test run.

An independent Python oracle passed on empty input and three seeded workloads:
1,842 / 100,000 / 160,000 rows, including all 1,842 endpoint IDs, all
72 hours, unsorted timestamps, latency extremes and error-rate boundaries.
The new program also passed GCC `-Wall -Wextra -Wpedantic -Werror` and a
full-size address/undefined-behavior sanitizer run. Local candidate source was
verified to match its committed Git blob hash exactly.

An alternate parser that skipped `madvise(MADV_DONTNEED)` improved one local
nine-round median from 109.92 ms to 98.95 ms, but increased process peak RSS
from approximately 84 MB to 295 MB. It was not retained because the official
scorer penalizes peak memory. An SSE4.2 CRC32C endpoint-hash experiment
matched output, but produced 105.94 ms versus the 105.68 ms baseline in a
separate twelve-round batch, so the original hashing was retained.

A branch-specific GitHub Actions correctness job was added but failed during
startup without usable runner logs. Consequently **no official sandbox run,
live-board run ID, or 90-point result is claimed**. The submission
`speed-demon` branch remains unchanged pending an official correctness and
performance comparison.
