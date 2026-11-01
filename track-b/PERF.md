# Track B: performance log

Every board run this track has scored, with the commit it measured. Judges can
check each run ID against the live board. Earlier local experiment logs are in
git history.

| Commit | Board run | T | M | Points | Notes |
|---|---|---|---|---|---|
| (reference) | | 56.35 s | 2,200 MB | 0 | Reference, unchanged |
| e8f4a84 (original C++) | run-04e59f26d537 | 6.275489 s | 404.82 MB | 27.840 | Original submission |
| dfdc930 | run-2c406b83bfaf | 0.806407 s | 205.30 MB | 50.651659 | 533 formatted source lines |
| 2ef4945 | run-d270ce788113 | 238.498 ms | 51,601,408 B | 69.244251 | S=647, D=1, K=24.259677 |
| 6759b53 | run-712fb991a4c8 | 231.992 ms | 51,568,640 B | **69.739111** | **Submitted version**; 610 lines |

## Submitted version

The submitted version is commit `6759b53`, scored at 69.739111 points on run
`run-712fb991a4c8`. It is the best Track B result on the board. The
requested 106 ms / 41 MB target is still unmet.

Run `run-d270ce788113` at `2ef4945` scored 69.244251 points. It was faster
and used less memory than the commit before it, but it has 37 more source lines
and scored lower overall, so it is not the submitted version.
