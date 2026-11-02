# Track D: performance log

Every step from the reference to your final version, each with the live-board run ID that measured it. Judges check that the run IDs exist and that each change moved the way you say it did.

| Step | Change | Run ID | T (s) | M (MB) | Points |
|---|---|---|---|---|---|
| 0 | The reference, unchanged | | 72.69 | 893 | 0 |
| 1 | Original C++ submission, e8f4a84 | run-db27d85c8981 | 8.748280 | 115.79 | 27.364 |

## Local candidate measurements (not board runs)

| Candidate | Process median (s) | Peak RSS (MiB) | Repetitions | Validation |
|---|---:|---:|---:|---|
| Previous implementation | 7.028240 | 110.66 | 3 | Full public seed-1 hash |
| Packed bidirectional CSR and sparse radix-queue searches | 0.564543 | 132.82 | 5 | Full public seed-1 hash on every run |
| Block-buffered graph scanning; final candidate | 0.356099 | 135.66 | 5 | Full public seed-1 hash on every run |

Command: `python scripts\benchmark.py d --runs 5` from the repository root.
Tiny input, independent all-pairs random-graph oracles (including full-width
weights and zero edges), strict GCC warnings and a production-style sandbox run
pass. Timings exclude Docker startup; hardware and host load differ from the
board; cross-batch ratios are indicative rather than controlled comparisons.
Final candidate is approximately 19.7x faster than the recorded local baseline
with higher RSS. Expanded parallel-construction oracles and ASan/UBSan also pass.
A dense-distance experiment was slower and was removed.
90 points and first place remain unverified.
Latest official-input recheck: five runs, median 0.326934 s, peak RSS 136.10 MiB.
Inputs and outputs verified against the live manifest linked from the board.
The published candidate's actual board result is recorded below.

## Confirmed board result and rejected local experiment

Commit `dfdc930`: run `run-c3a3efa5980d`, 0.418953 s, 141.78 MB,
365 formatted source lines, **57.008935 points**. Correctness passed.

A six-worker variant passed the official full output hash but measured
0.522989 s / 107.71 MiB versus twelve workers at 0.409246 s / 136.98 MiB.
The time penalty outweighed the memory benefit, so it was removed.

## Subsequent speed-first parser and search tuning

Complete buffered rows now parse in one numeric pass after finding the block's
last complete newline, instead of searching for every edge's newline first.
Search skips relaxations at or above the current best distance before consulting
the opposite frontier. A redundant radix-key runtime check was removed:
nonnegative Dijkstra weights already guarantee monotone inserted keys.
Input and output failure reporting remains.

Eight alternating runs per executable in one official-data sandbox:

| Executable | Median process time (s) | Maximum RSS (MiB) |
|---|---:|---:|
| Start-of-pass implementation | 0.459892 | 138.95 |
| Retained candidate | 0.436360 | 136.98 |

This is **5.1% lower median time**. An earlier controlled batch measured
0.386281 s before versus 0.379875 s after; the gain is modest and host-sensitive.
A separate final five-run batch measured 0.399363 s / 133.26 MiB.

Cached raw edges with per-worker degree arrays and prefix-owned CSR scatter
removed construction atomics but were slower and larger: controlled medians
0.475922 s / about 140 MB baseline, 0.505225 s / 304 MB local-degree candidate,
and 0.542144 s / 324 MB with full-width aligned edges. Both alternatives were
removed; packed six-byte CSR with sparse overflow weights remains.

Every full official hash passes. All 14 regression tests, all eight solution
methods under ASan/UBSan, strict warnings and all four offline production
sandbox checks pass. There is no new official score.

## Profile-guided queue and edge-storage compaction

Temporary phase timers measured 38-45 ms counting degrees, 17-18 ms allocating
and prefixing, 65-69 ms filling edges, and 229-252 ms answering queries.
Timers were removed. Radix entries now occupy twelve rather than sixteen bytes,
preserving all 64 distance bits. Finding a bucket minimum compares only distance;
node-ID tie comparisons never affected that minimum distance.
Edge arrays use overwrite allocation: checked degrees and unique atomic cursors
fill every slot before searches begin, so zero-initialization was redundant.

Eight alternating official-input runs per executable in one sandbox:

| Executable | Median process time (s) | Maximum RSS (MiB) |
|---|---:|---:|
| Start-of-pass implementation | 0.464529 | 135.01 |
| Packed queue and overwrite edge arrays | 0.450791 | 133.96 |

This is **3.0% lower median time**. An earlier three-way comparison measured
0.410595 s before, 0.407331 s with packed queues but zero-initialized arrays,
and 0.396743 s with both changes. A separate final five-run retained batch
measured 0.394181 s / 130.77 MiB. Total CPU time did not improve consistently;
elapsed gains and their transfer to scoring hardware remain host-dependent.

Own-distance-first relaxation and speculative sparse-distance prefetch were
exact but slower and were removed. No landmark preprocessing was added: the
entire measured query stage is only about a quarter second locally, so its
preprocessing cost would need a demonstrated whole-process win.
All official hashes, all 14 tests, all eight solution methods under ASan/UBSan,
strict warnings and all four offline sandbox checks pass. No new score exists.


## November 1: experiments that did not help

Reading ahead in the edge list (3 KB per edge) cost about 35 ms, allowing transparent huge pages raised peak memory by 7 MB for no gain, handing the arrays back in parallel before exit cost about 8 ms, cache-affine query batches and build-time PGO scored lower. The source is the best-scoring version.


## November 2: officially scored queue experiment and hardware-oriented research

The experimental 1,024-slot integer-wheel queue was correct and showed a 7.25%
median reduction in one paired GitHub Actions benchmark, but that **did not**
transfer to the scoring machine. Official board run `run-46320c1ba140`,
commit `9bb3f6a76188cd39fefd95b769f53135f1341b0f`, reported **0.177210 s**,
**77,762,560 bytes** and **532 code lines** for **67.732133 points**. Every gate
passed. The historical, better source (blob
`46bfcda6d77a5bfb6a72a4baf108de4d71c81a1c`) scored **68.316671 points**
at **0.174011 s**, **76,247,040 bytes** and **485 code lines** in official
run `run-e3a230f2ca26` on commit `6759b53eda0ced88ef30a8d0f9b77654a0c8686b`.
Commit `635c41cbfa8a7144e76f5fcdf2356cd73865cade` restored the historical
Track D source without modifying A–C. The board audit supporting this decision:
https://github.com/NewtonRedRing/speed-demon-starter/actions/runs/41436015800

New *experimental-branch only* tests, all using the official seed-1 full output
SHA-256 and reference-checked sample seeds, include:

- Wheel reset and sparse page sweep:
  https://github.com/NewtonRedRing/speed-demon-starter/actions/runs/41435656599 .
  Medians (ms): wheel baseline **442.562**, clear only **438.289**,
  128-entry pages **434.062**, 256-entry **444.871**, 1024-entry **436.421**.
- Independent repeatability check:
  https://github.com/NewtonRedRing/speed-demon-starter/actions/runs/41436119154 .
  Medians (ms): wheel baseline **637.903**, 64-entry pages **641.465**,
  128-entry pages **660.256**, 128+occupied-clear **645.766**. The prior
  128-page advantage **did not reproduce** on the wheel engine.
- Official-score-winning radix-heap experiments:
  https://github.com/NewtonRedRing/speed-demon-starter/actions/runs/41436898361 .
  Medians (ms): radix baseline **736.534**, 128-entry pages **721.316**,
  256-entry pages **732.583**, radix occupied-bucket bitmap **775.008**.
  The 128-page change is promising but has **no scoring-box proof**; the bitmap
  is clearly worse and was rejected. No experimental code was promoted.

**Research direction:** Efficient route preprocessing techniques such as
Customizable Tree Labeling (PVLDB 2025,
https://doi.org/10.14778/3748191.3748198), flow-optimized Customizable
Contraction Hierarchies (Procedia Computer Science 2025,
https://doi.org/10.1016/j.procs.2025.03.010), and batched customizable routing
(Future Transportation 2026, https://doi.org/10.3390/futuretransp6040134)
offer excellent road-network query acceleration, but their online index-build
time must be included in this contest's end-to-end wall clock. Since Track D
has just 2,000 predominantly local queries, prioritize CPU-cache locality,
lightweight exact distance storage, packed graph construction and minimizing
preprocessing overhead rather than importing a large routing index. A separate
controlled identity-order experiment showed ~**2.89x slower** performance when
BFS locality numbering was removed:
https://github.com/NewtonRedRing/speed-demon-starter/actions/runs/41436785189 .

**Promotion policy:** The official board's overall Track D machine points,
correctness gates, source-size count and memory, not GitHub-hosted runtime
alone, determine whether a tested implementation displaces the existing winner.
