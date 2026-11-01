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
