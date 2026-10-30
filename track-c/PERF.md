# Track C: performance log

Every step from the reference to your final version, each with the live-board run ID that measured it. Judges check that the run IDs exist and that each change moved the way you say it did.

| Step | Change | Run ID | T (s) | M (MB) | Points |
|---|---|---|---|---|---|
| 0 | The reference, unchanged | | 14.44 | 367 | 0 |
| 1 | Original C++ submission, e8f4a84 | run-1daf986bbf39 | 3.376534 | 393.50 | 13.136 |

## Local candidate measurements (not board runs)

| Candidate | Process median (s) | Peak RSS (MiB) | Repetitions | Validation |
|---|---:|---:|---:|---|
| Previous implementation | 3.299495 | 375.38 | 3 | Full public seed-1 hash |
| Quote-safe parallel parsing and exact shared name membership | 0.184681 | 15.32 | 5 | Full public seed-1 hash on every run |
| Read-mostly membership fast path; final candidate | 0.118340 | 14.94 | 5 | Full public seed-1 hash on every run |

Command: `python scripts\benchmark.py c --runs 5` from the repository root.
Tiny input, a repeated independent hostile CSV oracle, strict GCC warnings and a
production-style sandbox run pass. Timings exclude Docker startup; hardware and
host load differ from the board; cross-batch ratios are indicative rather than
controlled comparisons. Final candidate is approximately 27.9x faster than the
recorded local baseline and uses 25.1x lower peak RSS. The repeated CSV oracle also
passes ASan/UBSan. Hash-only/approximate distinct counts are not used.
90 points and first place remain unverified.
Latest official-input recheck: five runs, median 0.105275 s, peak RSS 15.16 MiB.
Inputs and outputs verified against the live manifest linked from the board.
The published candidate's actual board result is recorded below.

## Confirmed board result and rejected local experiments

Commit `dfdc930`: run `run-8bf4bf303f0c`, 0.109505 s, 17.04 MB,
370 formatted source lines, **62.331786 points**. Correctness passed.

Six workers and a smaller 1024-bucket dictionary were tested against official
inputs. Both passed exact hashes but lost more time than their memory savings
were worth. Alternating smaller-dictionary measurements yielded about -1.15
time/memory points. Both changes were removed; twelve workers and 4096 buckets
per stripe remain.

## Track C investigation toward 80 points

The feed generated `2026-10-30T22:17:55Z` recorded commit `1ba1831`,
run `run-2d891a7fe8f2`: **0.102775 s, 17,166,336 bytes, 370 formatted
source lines, 62.989248 points**. This is a published result, not a result for
the new local candidate.

Temporary phase timers on the official full seed-1 inputs measured:

| Published implementation phase | Elapsed range over five runs |
|---|---:|
| Serial region lookup loading | 10-13 ms |
| Quote prepass and shard preparation | 9-11 ms |
| CSV parsing and shared exact-name aggregation together | 75-84 ms |
| Merge and output | Under 1 ms |

These are elapsed phase measurements, not isolated profiles of parsing versus
dictionary operations. Timers were removed from the candidate. The dataset
contains 3,320,000 records, 100,520 exact names and 240 lookup countries; the
record stage cannot be dismissed as output formatting or Docker startup.

Retained changes: overlap region loading with quote parity, use dense base-26
country-code lookup, vector-friendly byte-XOR quote parity, `memchr` delimiter
scanning and stable deque-owned dictionary entries. Distinct names still use
full string equality and atomic country membership, not approximate hashes.

Controlled comparison: ten runs of each executable, alternating execution order
inside one sandbox using the same staged official inputs. Every output hash
passed.

| Executable | Median process time (s) | Maximum peak RSS (MiB) |
|---|---:|---:|
| Published implementation | 0.114407 | 15.16 |
| New local candidate | 0.104869 | 14.52 |

The local median reduction is **8.3%**, not a 4.8x speedup. A separate five-run
candidate batch measured 0.098435 s and 14.70 MiB; shared-host load makes
cross-batch timings unsuitable for controlled speedup claims.

Rejected alternatives included buffered record parsing with dynamically growing
long-record carry, cache-line-separated membership, and compact arena-backed
name views. They passed full hashes but did not justify their time, memory and
source-size tradeoffs. The buffered parser was removed rather than retaining
an extra implementation.

At the published memory/source axes, 80 points requires approximately
**0.021451 s** (K = 39.810717), versus the official 0.102775 s: about **4.79x**
faster. Source and memory changes alter that threshold. No new official score
is available, and 80 points remains unmet.

Validation: the full official input and output hashes passed all benchmark
repetitions and the production-style sandbox. All 14 regression tests passed.
The CSV oracle now includes quoted notes and names larger than 256 KiB,
embedded CR/LF and no final terminator; three repeated runs also pass ASan/UBSan.
Strict GCC warnings pass.

## Subsequent speed-first decoding and page-release tuning

The retained candidate clears decode scratch only when the first escaped quote
in a copied name is encountered. Plain names and unmaterialized notes do not
clear that buffer. Consumed-page releases now batch 1 MiB rather than 256 KiB,
start after the shard's initial partial page, and guard against unsigned
subtraction before the first full page is consumed.

Eight alternating official-input runs per executable in one sandbox:

| Executable | Median process time (s) | Maximum RSS (MiB) |
|---|---:|---:|
| Start-of-pass implementation | 0.132686 | 14.48 |
| Retained candidate | 0.128629 | 21.73 |

This is **3.1% lower median time**, deliberately trading additional resident
pages for raw speed. An earlier eight-run batch measured 0.098378 s before,
0.095941 s with lazy scratch clearing, and 0.094746 s with both changes.
A separate final five-run batch measured 0.125702 s / 21.77 MiB. Shared-host
load changes absolute times; these are not new board scores or points gains.

Deferred stripe-owned country deduplication was exact but slower
(0.102828 s versus 0.098791 s) and increased memory to about 72 MB, so it was
removed. The original exact atomic membership remains.
All official hashes, all 14 tests, all eight solution methods under ASan/UBSan,
strict warnings and all four offline production-sandbox checks pass.
