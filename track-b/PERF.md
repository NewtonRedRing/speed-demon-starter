# Track B: performance log

Every step from the reference to your final version, each with the live-board run ID that measured it. Judges check that the run IDs exist and that each change moved the way you say it did.

| Step | Change | Run ID | T (s) | M (MB) | Points |
|---|---|---|---|---|---|
| 0 | The reference, unchanged | | 56.35 | 2,200 | 0 |
| 1 | Original C++ submission, e8f4a84 | run-04e59f26d537 | 6.275489 | 404.82 | 27.840 |

## Local candidate measurements (not board runs)

| Candidate | Process median (s) | Peak RSS (MiB) | Repetitions | Validation |
|---|---:|---:|---:|---|
| Previous implementation | 5.749076 | 387.72 | 3 | Full public seed-1 hash |
| Phrase automaton plus adaptive Boolean postings | 1.095037 | 235.47 | 5 | Full public seed-1 hash on every run |
| Bounded phrase summaries, compact posting headers, four malloc arenas | 0.834935 | 202.97 | 5 | Full public seed-1 hash on every run |

Command: `python scripts\benchmark.py b --runs 5` from the repository root.
Tiny input, independent phrase/Unicode/Boolean and parallel bitmap-merge oracles,
strict GCC warnings and a production-style sandbox run pass. Timings exclude
Docker startup; hardware and host load differ from the board. Cross-batch ratios
are indicative, not controlled comparisons. Final candidate is approximately
6.9x faster than the recorded local baseline. Expanded empty-corpus/phrase-only/
Boolean-only oracles and ASan/UBSan checks also pass.
Profiling before header compaction found 78,820,996 bytes of posting payload and
35,914,368 bytes of posting headers across workers. The final candidate reduces
peak RSS but remains far above the requested target's memory footprint.
90 points and first place remain unverified.
Latest official-input recheck: five runs, median 0.776055 s, peak RSS 202.80 MiB.
Inputs and outputs verified against the live manifest linked from the board.
The published candidate's actual board result is recorded below.

## Confirmed board result and rejected local experiments

Commit `dfdc930`: run `run-2c406b83bfaf`, 0.806407 s, 205.30 MB,
533 formatted source lines, **50.651659 points**. Correctness passed.

Further official-input experiments passed output hashes but were rejected:

- Parallel per-term merging reduced memory but slowed total execution.
- Sequential per-term merging did not yield a repeatable score improvement.
- Six workers reduced memory but lost the benefit to slower execution in
  alternating measurements.
- Inline singleton postings saved allocations, but their small time/memory
  benefit did not justify the added source-size penalty.

These experiments were removed; subsequent retained changes are described below.
All comparisons used live official manifest hashes. Host-load variation was
large enough that isolated faster runs were not treated as improvements.

## Speed-first mapped scanning and exact dense summaries

The retained candidate maps the corpus once and scans aligned shards directly,
removing per-document line allocations/copies. Full consumed pages are released
in 1 MiB batches, keeping shard boundary pages intact. Dense Boolean summaries
compute exact count and ID sum with population counts and six weighted masks;
words with fewer than eight matches retain set-bit enumeration. The first ten
IDs still use ascending set bits. No approximate membership or result is used.

Eight alternating runs per executable in one official-data sandbox:

| Executable | Median process time (s) | Maximum RSS (MiB) |
|---|---:|---:|
| Start-of-pass implementation | 1.238990 | 202.94 |
| Retained candidate | 1.204293 | 200.67 |

This is **2.8% lower median time**. An earlier three-candidate controlled batch
measured 1.152035 s before, 1.116037 s with population summaries, and 1.086487 s
with mapped scanning as well. A byte-classification lookup table did not improve
the mapped version and was removed. A separate five-run retained batch measured
1.072819 s / 207.26 MiB; absolute cross-batch timings vary with host load.

All full official output hashes pass. All 14 regression tests, all eight
solution methods under ASan/UBSan, strict GCC warnings and all four offline
production-sandbox checks pass. The B oracle additionally checks all six bit
positions, sparse seven-bit words, full words and a partial final word.
There is no new official score.
