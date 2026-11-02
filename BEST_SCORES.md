# Speed Demon 2026: best board result per track

Team `NewtonRedHeat`. Best runs on the public board (full public dataset, seed 1), as of November 2, 09:27 UTC:

| Track | Commit | Points | Time | Peak memory |
|---|---|---:|---:|---:|
| A | `df4943d` | 82.763241 | 23.96 ms | 19.4 MB |
| B | `f8ae231` | 71.694746 | 191.08 ms | 50.9 MB |
| C | `63ec07b` | 73.956052 | 37.853 ms | 10.22 MB |
| D | `6759b53` | 68.316671 | 174.01 ms | 76.2 MB |

Every run is in the track's PERF.md. The branch tip holds the sources that produced these results (A: parallel input release, static binary; C: fused 64-byte AVX2 structural scanner, parallel input release; B and D unchanged from their best runs). Board times vary by a few percent between runs of identical source, so the final scoring run of the tip may differ slightly from these bests.
