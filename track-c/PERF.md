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
**Publication authorized; board run ID and points pending.**
