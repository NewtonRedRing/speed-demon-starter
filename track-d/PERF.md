# Track D: performance log

Every board run this track has scored, with the commit it measured. Judges can
check each run ID against the live board. Earlier local experiment logs are in
git history.

| Commit | Board run | T | M | Points | Notes |
|---|---|---|---|---|---|
| (reference) | | 72.69 s | 893 MB | 0 | Reference, unchanged |
| e8f4a84 (original C++) | run-db27d85c8981 | 8.748280 s | 115.79 MB | 27.364 | Original submission |
| dfdc930 | run-c3a3efa5980d | 0.418953 s | 141.78 MB | 57.008935 | 365 formatted source lines |
| 2ef4945 | run-b47491020917 | 177.553 ms | 75,284,480 B | 68.113015 | Track D source unchanged; S=491, D=1, K=23.028217 |
| 6759b53 | run-e3a230f2ca26 | not recorded | not recorded | **68.316671** | **Submitted version** |

## Submitted version

The submitted version is commit `6759b53`, scored at 68.316671 points on run
`run-e3a230f2ca26`. It is the best Track D result on the board. The requested
120 ms / 66 MB target is still unmet.

Commit `2ef4945` did not change Track D's source. Its re-run scored 68.113015
points, below the submitted version.

## November 1: local distance-page experiments

The official monotonic timer, prewarmed Linux-volume input, fresh sandboxed
containers and 12 interleaved runs per image gave:

| Distance page | Trimmed time | Trimmed memory |
|---|---|---|
| 512 nodes (protected baseline) | 188.073 ms | 76,818,944 B |
| 64 nodes | 186.250 ms | 75,510,272 B |
| 128 nodes | 189.005 ms | 75,050,496 B |

All matched the official full seed-1 output hash. The gains were too small
relative to local variability to promote, and neither changes which three
tracks contribute to our overall score. The original 512-node page remains.
