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
| Name table 2^18 slots (load 0.38 instead of 0.77); in-container median 26.9 ms vs 29.2 ms | n/a (in-container) | 215.8 vs 208.9 (VmHWM incl. mapped input) | 7 | Full public seed-1 hash on every run; `check.py c --runs 3` PASS |

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

## Profile-guided compile-time CSV modes

Current phase timers measured 12-14 ms loading regions/preparing boundaries,
88-98 ms parsing/aggregating names, and under 1 ms merging/writing. Timers were
removed. The retained field parser specializes copied-name and length-only note
handling at compile time, so the escaped-field loops do not test a runtime
materialization flag. Exact quote decoding and full name equality are unchanged.

Eight alternating official-input runs per executable in one sandbox:

| Executable | Median process time (s) | Maximum RSS (MiB) |
|---|---:|---:|
| Start-of-pass implementation | 0.125930 | 22.55 |
| Specialized field modes | 0.121983 | 21.52 |

This is **3.1% lower median time**. An earlier controlled batch measured
0.107760 s before versus 0.104724 s after. The separate final five-run median
was 0.113996 s / 21.89 MiB. Shared-host load varies between batches.

Rejected experiments: a 1,024-entry per-worker exact-name cache did not reduce
elapsed time; inline name hashing lost its small initial gain in a repeat;
a 2,048-bucket dictionary was slower. All were removed, retaining the exact
4,096-bucket stripes. The earlier cleanup also avoids the final shard's unused
quote-parity scan and its extra prepass worker.

All official hashes, all 14 tests, all eight solution methods under ASan/UBSan,
strict warnings and all four offline sandbox checks pass. No new official
score or 80-point result is claimed.

## 2026-10-31: Exact long-name equality and mapped EOF

Names over 16 bytes now store their actual bytes 8..16 in the second table
word. Combined with their first eight bytes and the existing arena comparison
of bytes 16..end, the equality decision is exact even when CRC hashes collide.
The file mapping includes a spare zero-filled page. The final segment is
parsed in place without a copied tail buffer; 64-byte SIMD loads remain
within the reserved mapping. The PGO image and its pinned digests are retained.
This change is not yet supported by a new Rust build, official hash or measured
speed improvement; these remain to validate on the board.

### 2026-10-31: Revert zero-copy EOF mapping after board regression

Run `run-0b1d0e65fdba` at `39502cdb` passed every correctness gate, but
had M=17,747,968 B and scored 68.996326; best earlier run
`run-0917a954f355` had M=11,579,392 B and scored 70.830007.
This comparison includes changed PGO training and exact-name logic, so it
cannot isolate the mmap experiment. Restore the original copied/padded tail
and best single-seed PGO build; retain the exact long-name byte comparison
and separately verified BMI2 PDEP parity formula. No final gain claimed
until another board run.

## 2026-11-01: isolated tmpfs-output experiments

Nine adjacent alternating pairs per candidate, after excluded warmup, used the unchanged
offline PGO Dockerfile and exact original public seed-1 input/output hashes.
Output is freshly created on tmpfs every run; unexpected output entries fail.
Local trimmed T discards the fastest and slowest observations; MB is decimal.
The baseline pools eighteen observations across both candidates.

| Candidate | Local trimmed T | Maximum observed cgroup peak |
|---|---:|---:|
| Baseline | 76.269 ms | 13.836 MB |
| Input views for long names | 72.503 ms | 12.214 MB |
| Batch size 32 instead of 16 | 77.839 ms | 13.685 MB |

Long-name input views passed forced hash/prefix-collision and concurrent
publication checks. Their timing changed direction across batches: the final
paired time ratio was 0.9620 (interval 0.8891..1.0927), and complete local K
ratio 1.0426 (0.9758..1.0878), still crossing no improvement.
Larger batching also did not establish a gain. Neither change was retained.
The production source is unchanged and passed five final full official-data
checks. The local i7 VM is not the scoring i5-13500, and its observed cgroup
peak is not the official post-removal parent-cgroup measurement.

```powershell
python scripts\investigate_tracks.py --tracks c --runs 9 --variants baseline long-name-views batch-32 --output .out\c-experiments.json
```

The runner defaults to immutable baseline
`7a9886d0635e3deb74493f2c23cde76c30c521c2` and tmpfs output.
The final report `.out/four-track-fresh-output.json` supersedes earlier
reused-output measurements, whose memory accounting could inherit page charges.
The 20 ms/11 MB target has not been established.

## Official board result — commit `2ef4945`

Track C source was unchanged by this push. The board nevertheless ran and
scored it successfully: `run-c08453a4bb25`, T=48.717 ms,
M=11,825,152 B, S=677, D=1, K=25.995882, 70.745228 points.
All official gates passed. This is below the existing best
`run-7a3979243031` (70.905419 points). The 20 ms/11 MB target remains unmet.
