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
| 6759b53 | run-712fb991a4c8 | 231.992 ms | 51,568,640 B | 69.739111 | Previous best; 610 lines |
| 0e3b9ac | run-56343bb059d4 | 209.118 ms | 51,953,664 B | **70.780882** | **Submitted version**; S=620, D=1 |

## Submitted version

The submitted version is commit `0e3b9ac`, scored at 70.780882 points on run
`run-56343bb059d4`, up 1.041771 points from the previous best. All board gates
passed. This is our best Track B result, not the contest's track record. The
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

The board confirmed the improvement: 209.118 ms, 51,953,664 bytes, S=620, D=1
and 70.780882 points, recorded above.

Correctness matches the official full seed-1 hash and the official reference on
tiny seeds 2, 7, 42 and 2026 and sample seeds 17 and 103.

Rejected local alternatives: a 512-entry exact token cache, a second dictionary
prefetch stage, a prefix-only candidate hash, a 32-token prefetch lag, smaller
posting buffers, and generator-trained PGO. None justified its time, memory or
formatted-source cost. They are not in the submission.

## November 1: compact dictionary candidate

Each dictionary slot now packs a 14-bit hash tag and 18-bit term ID plus one
into 32 bits instead of 64. Collisions still require full exact token equality.
The existing 131,072-term limit fits with room for the empty-slot sentinel.
Canonical formatted source is S=625, five lines above the protected version.

Two globally shuffled local comparisons disagreed on time: 199.789 / 186.842 ms
and 196.805 / 201.421 ms (protected / candidate). To control temporal drift, a
third comparison grouped runs into 24 pairs with independently randomized order
inside each pair. The candidate's median paired time ratio was 0.96680 and
mean ratio 0.97168; a seed-42, 10,000-resample paired bootstrap gave a 95%
mean-ratio interval of [0.95183, 0.99116]. It was faster in 16 of 24 pairs.
Trimmed means were 268.690 / 258.771 ms and 53,147,136 / 52,154,368 bytes.
Absolute local times vary substantially with host conditions.

The official sandbox/full seed-1 hash and reference comparisons on tiny seeds
2, 7, 42, 2026, sample seeds 17, 103 and full seed 23 all passed.
This remains a candidate until it beats the protected 70.780882 board points.

A separate cache-line-fused dictionary was rejected: 180.546 / 187.846 ms
and 53,586,432 / 68,510,208 bytes, with matching official output hashes.
