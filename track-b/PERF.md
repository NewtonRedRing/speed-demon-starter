# Track B: performance log

Every step from the reference to your final version, each with the live-board run ID that measured it. Judges check that the run IDs exist and that each change moved the way you say it did.

| Step | Change | Run ID | T (s) | M (MB) | Points |
|---|---|---|---|---|---|
| 0 | The reference, unchanged | | 56.35 | 2,200 | 0 |
| 1 | Original C++ submission, e8f4a84 | run-04e59f26d537 | 6.275489 | 404.82 | 27.840 |

## Local candidate measurements (not board runs)

| Candidate | Process median (s) | Peak RSS (MiB) | Repetitions | Validation |
|---|---:|---:|---:|---|
| Previous implementation | 5.749076 | 387.72 | 3 | Full public seed-1 hash |
| Phrase automaton plus adaptive Boolean postings | 1.095037 | 235.47 | 5 | Full public seed-1 hash on every run |
| Bounded phrase summaries, compact posting headers, four malloc arenas | 0.834935 | 202.97 | 5 | Full public seed-1 hash on every run |

Command: `python scripts\benchmark.py b --runs 5` from the repository root.
Tiny input, independent phrase/Unicode/Boolean and parallel bitmap-merge oracles,
strict GCC warnings and a production-style sandbox run pass. Timings exclude
Docker startup; hardware and host load differ from the board. Cross-batch ratios
are indicative, not controlled comparisons. Final candidate is approximately
6.9x faster than the recorded local baseline. Expanded empty-corpus/phrase-only/
Boolean-only oracles and ASan/UBSan checks also pass.
Profiling before header compaction found 78,820,996 bytes of posting payload and
35,914,368 bytes of posting headers across workers. The final candidate reduces
peak RSS but remains far above the requested target's memory footprint.
90 points and first place remain unverified.
Latest official-input recheck: five runs, median 0.776055 s, peak RSS 202.80 MiB.
Inputs and outputs verified against the live manifest linked from the board.
The published candidate's actual board result is recorded below.

## Confirmed board result and rejected local experiments

Commit `dfdc930`: run `run-2c406b83bfaf`, 0.806407 s, 205.30 MB,
533 formatted source lines, **50.651659 points**. Correctness passed.

Further official-input experiments passed output hashes but were rejected:

- Parallel per-term merging reduced memory but slowed total execution.
- Sequential per-term merging did not yield a repeatable score improvement.
- Six workers reduced memory but lost the benefit to slower execution in
  alternating measurements.
- Inline singleton postings saved allocations, but their small time/memory
  benefit did not justify the added source-size penalty.

These experiments were removed; subsequent retained changes are described below.
All comparisons used live official manifest hashes. Host-load variation was
large enough that isolated faster runs were not treated as improvements.

## Speed-first mapped scanning and exact dense summaries

The retained candidate maps the corpus once and scans aligned shards directly,
removing per-document line allocations/copies. Full consumed pages are released
in 1 MiB batches, keeping shard boundary pages intact. Dense Boolean summaries
compute exact count and ID sum with population counts and six weighted masks;
words with fewer than eight matches retain set-bit enumeration. The first ten
IDs still use ascending set bits. No approximate membership or result is used.

Eight alternating runs per executable in one official-data sandbox:

| Executable | Median process time (s) | Maximum RSS (MiB) |
|---|---:|---:|
| Start-of-pass implementation | 1.238990 | 202.94 |
| Retained candidate | 1.204293 | 200.67 |

This is **2.8% lower median time**. An earlier three-candidate controlled batch
measured 1.152035 s before, 1.116037 s with population summaries, and 1.086487 s
with mapped scanning as well. A byte-classification lookup table did not improve
the mapped version and was removed. A separate five-run retained batch measured
1.072819 s / 207.26 MiB; absolute cross-batch timings vary with host load.

All full official output hashes pass. All 14 regression tests, all eight
solution methods under ASan/UBSan, strict GCC warnings and all four offline
production-sandbox checks pass. The B oracle additionally checks all six bit
positions, sparse seven-bit words, full words and a partial final word.
There is no new official score.

## Profile-guided allocator and worker-storage cleanup

Temporary phase timers measured 650-703 ms scanning the corpus, 137-158 ms
merging, 40-51 ms parsing queries/building the automaton, 39-65 ms evaluating
queries and 3-5 ms writing. Timers were removed.

The retained arena limit is twelve instead of four, reducing contention among
scanner allocations. After the unchanged worker-major merge, independent
worker posting storage is destroyed in parallel. This is not the previously
rejected parallel per-term merge; no target posting is concurrently mutated.
ASan continues to skip glibc-specific allocator tuning.

Eight alternating runs per executable in one official-data sandbox:

| Executable | Median process time (s) | Maximum RSS (MiB) |
|---|---:|---:|
| Start-of-pass, four arenas | 1.230033 | 197.38 |
| Twelve arenas, serial worker cleanup | 0.961598 | 219.66 |
| Twelve arenas, parallel worker cleanup | 0.949081 | 219.41 |

The retained candidate is **22.8% lower median time**, with approximately
22 MiB additional RSS. An earlier three-way batch measured 1.072565 /
0.870283 / 0.854985 s respectively. The final separate five-run candidate
median was 0.774453 s / 219.74 MiB. Cross-batch absolute timings are not speedup
evidence. Inline token hashing measured effectively equal to the standard hash
and was removed rather than adding an unnecessary algorithm.

All full official hashes, all 14 tests, all eight solution methods under
ASan/UBSan, strict warnings and all four offline sandbox checks pass. No new
official score exists; the memory tradeoff must be included when judging points.

## Complete source audit: flat posting construction

The full source was reviewed, not just output formatting. Corrected shard-level
diagnostics on the full official input found approximately 36.3 million tokens,
28.7 million known query-token occurrences and 21.1 million distinct Boolean
term/document memberships. There are 75,747 query terms, 53,444 Boolean terms
and 19,000 phrase queries. The automaton has 54,509 nodes: 18,886 leaves,
34,225 single-child nodes and 1,398 branching nodes.

This explains why B is not comparable to simply parsing 250,000 document IDs:
it performs tens of millions of exact dictionary, phrase and membership
operations over 253.8 MB of text.

### Findings across the source

| Source area | Finding and disposition |
|---|---|
| `mix`, `Transitions`, `Automaton` | Exact failure/output behavior is necessary. Inline single-child transitions did not give a reliable whole-process win; removed. |
| `Query`, `Term`, `Words` | Query-only dictionary and fingerprint-plus-full-equality checks retained. No hash-only matches or truncated long-token matches. |
| `Matches` | Count/sum/first-ten summaries and per-document phrase deduplication retained, including overlapping and duplicate queries. |
| `Posting` | Per-hit vector growth and incremental promotion/merge methods removed. Final representations are now built once. |
| `parallel` | Worker joins and exception propagation retained; they prevent silent partial results. |
| `Local`, `scan` | Replaced one dynamic posting per term per worker with a flat hit vector and compact counts. ASCII/Unicode separator semantics and mapping bounds retained. |
| Partition/build stages in `solve` | Worker-local prefix/scatter preserves document order; batched term owners build final postings in parallel without shared writes. |
| `apply`, `format_bits` | Left-to-right Boolean operations and exact dense population summaries retained. |
| Query parsing, mapping, output, `main` | Required syntax, I/O and error checks retained. Output is still exactly one deterministic file. |

Temporary phase timers, removed from retained source, measured:

| Stage | Before redesign | Flat-layout candidate |
|---|---:|---:|
| Query preparation | 55-65 ms | 43-89 ms |
| Corpus scan | 709-730 ms | 368-485 ms |
| Local partition | Not present | 20-25 ms |
| Serial merge | 120-127 ms | Removed |
| Final posting construction and cleanup | Cleanup alone 6-7 ms | 18-25 ms |
| Query evaluation | 42-44 ms | 33-45 ms |
| Output | 4-5 ms | 2-3 ms |

These are diagnostic ranges from separate batches, not controlled stage-specific
speedups. Before-redesign counters also add instrumentation overhead. The
uninstrumented total-process comparisons below establish the retained gain.

### Retained algorithm and controlled result

Scanning appends one compact hit per distinct Boolean term/document pair.
Each worker count/prefix-scatters its own hits into contiguous ID slices,
without cross-worker cache-line contention. Workers then claim 32-term batches:
each final term is either reserved once and filled from sorted slices, or
constructed directly as a bitmap. There is no comparison sort and no per-ID
merge into a growing global vector. Temporary hit buffers and posting slices
are released at their last use.

Eight alternating runs per executable in one sandbox, using staged official
full seed-1 inputs and live manifest hashes for every output:

| Executable | Median process time (s) | Maximum RSS (MiB) |
|---|---:|---:|
| Published `b03b834` implementation | 0.786195 | 219.28 |
| Flat collection/scatter and one-shot posting build | 0.625108 | 280.30 |

This is **20.5% lower elapsed time (1.26x throughput)**, trading about 61 MiB
for speed. Two earlier controlled batches measured 0.807776 to 0.632046 s and
0.804912 to 0.638640 s, confirming roughly 20.5-21.8% reductions.
A separate final five-run batch measured 0.509163 s / 278.18 MiB.
No new official score exists; memory and formatted-source penalties still
affect points, and the shared local host is not the scoring CPU.

### Rejected alternatives and validation

Inline single-child automaton transitions, per-worker monotonic allocation
pools, bounds-safe AVX2 token scanning, bulk insertion into the old posting
layout and moving document stamps into posting headers were tested. They either
did not improve repeatably or increased memory without enough time benefit.
All were removed. The retained redesign differs from earlier rejected parallel
term merges: it removes the per-worker posting-vector allocation/growth scheme
and presents already-partitioned contiguous slices to the final builders.

All full official output hashes pass. All 14 regression tests, all three B
oracle methods under ASan/UBSan, strict GCC warnings and the allowlisted offline
production-style sandbox pass. Added coverage exercises global density choices
with sparse worker slices, 255/256 memberships, a nonzero bitmap base, duplicate
tokens, partial final words and mixed sparse/dense Boolean operations.

## October 31 speed-first experiment: perfect short-token lookup (draft)

The experimental `Words` lookup now uses a collision-free, fixed base-36 array
for every queried token of length 1-3 (digits and lowercase ASCII). A length
offset makes all 47,988 possible keys distinct: 36 + 1,296 + 46,656.
Tokens longer than three bytes use the existing exact fingerprint and string
equality hash table. Short tokens absent from queries still return `missing`.

The optimization attacks the approximately 36.3M-token full scan by avoiding
hashing and hash-table probing on short query terms. The perfect-map indexing
was independently enumerated across all 47,988 possible keys, with no
collisions. A lookup-only synthetic benchmark with 60% short query tokens
measured 681.2 ms baseline versus 480.8 ms with the fast path across 20M
lookups. A contrasting distribution dominated by long tokens measured
573.9 ms versus 596.3 ms (regression). Neither distribution is the official
corpus, and **no whole-process speed improvement or official score is claimed**.

Before promotion: compile with strict warnings, run all independent B oracles,
verify seed-1 full result SHA-256, and run alternating official performance
comparisons against the submitted version. Keep it only if the complete
scoring coefficient improves.


## Second-round speed-first study (October 31)

Adds a chunked, exact-collision-checked 64-bit query-token fingerprint in
`Words`. The dictionary's insertion fingerprint and lookup fingerprint
use the same function; full token-string equality remains required before
returning a hit. The earlier direct base-36 lookup for length <=3 remains
on this branch. Very long scanned tokens retain the existing `missing`
fallback, so ASCII tokenization semantics are unchanged.

A standalone local 20M-token hash-only microbenchmark (random 4-17-byte
tokens with a hot dictionary) measured medians of 415.8 ms using
`std::hash<string_view>` versus 372.0 ms with the custom word-at-a-time
hash, a 10.5% isolated hash reduction. The experiment has **not** been
benchmarked on official query and corpus files; scanner and automaton costs
could erase the gain. No new machine score is claimed.

Promotion gate: full official output hash, independent UTF-8 and
phrase/Boolean oracles, sanitizer run, then alternating complete-process
benchmarks and measured K coefficient.
