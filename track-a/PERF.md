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
**Publication authorized; board run ID and points pending.**
