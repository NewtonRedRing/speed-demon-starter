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

## October 31 speed-first experiment: mapped graph construction (draft)

The degree-counting and CSR-filling graph passes now read from a shared
read-only `mmap` of `graph.txt` instead of 12 independent `FILE*` handles,
repeated 256 KiB `fread` buffers and copies. Workers skip an initial partial
edge line and finish the final edge line whose start lies in their byte
partition. All atomics, parallel edges, full uint32 overflow weights, queue
logic and exact 64-bit search distances remain unchanged. The mapping is
released immediately after constructing both CSR graphs.

A randomized alignment oracle verified one-to-one coverage for byte-partitioned
edge streams across multiple graph sizes and worker counts. A standalone
microbenchmark with 5,000,000 valid synthetic edges and 12 parallel workers
measured 26.23 ms for buffered scanning and 24.40 ms for mapped scanning,
with identical parsed checksums. A *sequential* counterpart on 1M edges
regressed (14.91 ms buffered vs 17.21 ms mapped), so the improvement is
sensitive to memory layout and parallelism. The graph-only microbenchmark
does not include degree atomics, CSR construction or queries.

The full mapped input could raise the official cgroup memory high-water mark,
potentially offsetting the runtime benefit. Test the exact official hash,
correctness oracles, and complete-process K before promotion. **No verified
official score or global speedup is claimed.**
