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

## November 1: role-specific scan candidate

Bitmap terms now only OR membership bits; list terms alone deduplicate and
append postings. Unused terms and phrase-only anchors create no posting state.
The obsolete masked-write path was removed.

Local interleaved Linux-volume runs with the official timer and fresh sandboxed
containers consistently favored this approach, although the margin varied.
One 12-run-per-image comparison gave trimmed means of 217.290 ms for the
protected baseline and 205.659 ms for the role-specific scan (5.4% lower).
The final simplified writer was rechecked with 12 runs per image: 245.374 ms
versus 230.712 ms (6.0% lower), with essentially unchanged trimmed memory
(53,217,792 versus 53,248,512 bytes). These are local measurements, not board
results.

Correctness matches the official full seed-1 hash and the official reference on
tiny seeds 2, 7, 42 and 2026 and sample seeds 17 and 103.

Rejected local alternatives: a 512-entry exact token cache, a second dictionary
prefetch stage, a prefix-only candidate hash, a 32-token prefetch lag, smaller
posting buffers, and generator-trained PGO. None justified its time, memory or
formatted-source cost. They are not in the submission.
