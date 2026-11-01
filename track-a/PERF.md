# Track A: performance log

Every board run this track has scored, with the commit it measured. Judges can
check each run ID against the live board. Earlier local experiment logs are in
git history.

| Commit | Board run | T | M | Points | Notes |
|---|---|---|---|---|---|
| (reference) | | 32.05 s | 228 MB | 0 | Reference, unchanged |
| e8f4a84 (original C++) | run-93a4bea20d70 | 0.628347 s | 34.73 MB | 48.326 | Original submission |
| dfdc930 | run-fa8a1891ec46 | 0.078022 s | 52.05 MB | 66.618152 | |
| 39502cdb | run-3b8f1a6bdd06 | 0.030782 s | 22,573,056 B | 79.269335 | Pair-digit output; rejected |
| 73169015 | run-5fbf4e3e8868 | 0.030690 s | 21,938,176 B | 79.686448 | Best before the submitted version |
| a2c6f51d | run-c42954603af9 | 0.046989 s | 22,585,344 B | 74.238468 | Lower than the best |
| 2ef4945 | run-a3f9ee9d24f4 | 30.669 ms | 18,886,656 B | **80.334988** | **Submitted version**; S=349, D=1, K=40.429629 |

## Submitted version

The submitted version is commit `2ef4945`, scored at 80.334988 points. It
passed all build, correctness, timeout and sandbox gates. It beats the previous
best (`run-5fbf4e3e8868`, 79.686448) by 0.648540 points. The scored time is
essentially unchanged from that best; the lower memory drives the gain. The
16 ms / 18 MB target is still unmet.

## Rejected board experiments

- `39502cdb`, pair-digit output (`run-3b8f1a6bdd06`): 79.269335. The writer was
  restored to simple single-digit emission.
- `a2c6f51d` (`run-c42954603af9`): 74.238468, below the best.

## November 1: rejected local experiments

These are local Docker results, not board runs. Except for the first hint
experiment, measurements used the official monotonic timer, prewarmed
Linux-volume data, fresh sandboxed containers, tmpfs output and interleaved
baseline/candidate runs. Those interleaved candidates matched the official
full seed-1 hash.

| Experiment | Baseline / candidate trimmed time | Decision |
|---|---|---|
| Parallel `MADV_POPULATE_READ` | 22.533 / 30.869 ms | Slower; removed |
| SWAR latency fast path | 27.005 / 25.423 ms | Gain too small and inconsistent to promote |
| Hour-indexed direct aggregation | 24.437 / 36.214 ms | Slower and higher peak memory; removed |
| Field-specific scan and timestamp arithmetic | 45.919 / 46.396 ms | No improvement; removed |
| Smaller summaries with eager allocator release | 32.445 / 32.790 ms | No time or peak-memory gain; removed |
| Endpoint hash tags | 27.266 / 27.059 ms | Inconclusive; removed |
| Endpoint hash tags plus SWAR | 27.266 / 27.947 ms | No improvement; removed |
| Full-scale generator-trained PGO | 25.313 / 24.723 ms | Too small to justify source/build cost and noise |
| Median-first percentile selection | 24.336 / 24.214 ms | Inconclusive; removed |
| Six or eight workers | 29.210 / 35.939 / 31.199 ms (12 / 6 / 8) | Slower; 12 retained |
| Multiply-xorshift endpoint hash | 25.854 / 26.244 ms | Slower than CRC32C; removed |
| 4 KB instead of 16 KB record chunks | 23.683 / 23.436 then 26.722 / 26.913 ms | Time and memory inconclusive; 16 KB retained |

No Track A code change from these experiments was submitted.
