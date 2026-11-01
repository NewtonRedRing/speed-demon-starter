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
| 6759b53 | run-e3a230f2ca26 | 174.011 ms | 76,247,040 B | **68.316671** | **Submitted version**; S=485, D=1 |
| c915724 | run-8d7df9f46a1d | 178.043 ms | 76,292,096 B | 68.065400 | 1,024-node blobs only; below best |
| 8f6a12b | run-05c08d78cd3f | 177.661 ms | 74,711,040 B | 68.179668 | 1,024-node blobs + 64-node pages; below best |

## Submitted version

The submitted version is commit `6759b53`, scored at 68.316671 points on run
`run-e3a230f2ca26`. It is our best Track D result on the board. The requested
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

## November 1: larger BFS locality blobs

Changing the BFS locality target from 512 to 1,024 nodes matched the official
full seed-1 hash in every benchmark and passed the official sandbox gate plus
reference comparisons on tiny seeds 2, 7, 42, 2026, sample seeds 17 and 103,
and full seed 23.

In the first 16-run comparison (protected / 256 / 1,024 nodes), trimmed times
were 255.011 / 252.543 / 246.469 ms. A separate 24-pair baseline comparison
gave 262.501 / 250.469 ms; across both comparisons, 1,024-node blobs were
faster in 30 of 40 pairs, with median paired ratio 0.96103, mean ratio
0.93546, and paired bootstrap 95% interval [0.87964, 0.97667]. Trimmed
memory was about 0.21 MB higher. A single 16-run 2,048-node trial was
inconclusive. The 1,024-node-only variant scored 68.065400 points on the board,
and the 1,024-node/64-node-page combination scored 68.179668. Both were below
the protected 68.316671, so the 512-node blobs and 512-node distance pages are
restored. The local combination appeared faster and smaller in one 16-pair
test (184.993 / 181.030 ms and 76,814,746 / 76,208,128 B; protected /
candidate), but board results decide promotion.
