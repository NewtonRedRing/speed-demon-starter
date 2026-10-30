# Track D: performance log

Every step from the reference to your final version, each with the live-board run ID that measured it. Judges check that the run IDs exist and that each change moved the way you say it did.

| Step | Change | Run ID | T (s) | M (MB) | Points |
|---|---|---|---|---|---|
| 0 | The reference, unchanged | | 72.69 | 893 | 0 |
| 1 | Original C++ submission, e8f4a84 | run-db27d85c8981 | 8.748280 | 115.79 | 27.364 |

## Local candidate measurements (not board runs)

| Candidate | Process median (s) | Peak RSS (MiB) | Repetitions | Validation |
|---|---:|---:|---:|---|
| Previous implementation | 7.028240 | 110.66 | 3 | Full public seed-1 hash |
| Packed bidirectional CSR and sparse radix-queue searches | 0.564543 | 132.82 | 5 | Full public seed-1 hash on every run |
| Block-buffered graph scanning; final candidate | 0.356099 | 135.66 | 5 | Full public seed-1 hash on every run |

Command: `python scripts\benchmark.py d --runs 5` from the repository root.
Tiny input, independent all-pairs random-graph oracles (including full-width
weights and zero edges), strict GCC warnings and a production-style sandbox run
pass. Timings exclude Docker startup; hardware and host load differ from the
board; cross-batch ratios are indicative rather than controlled comparisons.
Final candidate is approximately 19.7x faster than the recorded local baseline
with higher RSS. Expanded parallel-construction oracles and ASan/UBSan also pass.
A dense-distance experiment was slower and was removed.
90 points and first place remain unverified.
Latest official-input recheck: five runs, median 0.326934 s, peak RSS 136.10 MiB.
Inputs and outputs verified against the live manifest linked from the board.
The published candidate's actual board result is recorded below.

## Confirmed board result and rejected local experiment

Commit `dfdc930`: run `run-c3a3efa5980d`, 0.418953 s, 141.78 MB,
365 formatted source lines, **57.008935 points**. Correctness passed.

A six-worker variant passed the official full output hash but measured
0.522989 s / 107.71 MiB versus twelve workers at 0.409246 s / 136.98 MiB.
The time penalty outweighed the memory benefit, so it was removed.
