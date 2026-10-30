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
**Publication authorized; board run ID and points pending.**
