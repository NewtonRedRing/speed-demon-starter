# Track B: exact inline-eight posting buffers (October 31, 2026)

**Status: experimental production-source patch, NOT promoted to `speed-demon`.**
The official seed-1 full corpus and its expected results hash were unavailable
to this execution environment. Existing GitHub Actions workflows fail before
running jobs. No whole-track correctness or leaderboard claim is made.

## New design

The existing worker-local Boolean indexing pipeline stores each
`(term, document)` pair in an eight-byte flat `Hit` array, counts hits,
then scatters into a second four-byte-per-hit posting array. During scatter,
both allocations are live, causing a large peak.

Replace the worker's two temporary arrays with one **exact per-term
`SmallPosting`**: a 40-byte header containing eight inline 32-bit document
IDs and a heap pointer in a union. At the ninth occurrence, allocate 16 IDs
and grow geometrically. Append each deduplicated document ID directly while
tokenizing. A `std::span<const u32>` exposes the resulting sorted list to
the existing sparse/bitmap global posting merger. There is no sorting,
hash-only acceptance, or lossy compression.

The production patch in this branch changes only `track-b/src/main.cpp`.
It leaves phrase matching, ASCII tokenization, Boolean left associativity,
final posting selection, query formatting and worker cleanup unchanged.
The submission branch remains at the board-verified rollback
`425051dc91a0713b1c4e97e57308221a3df71dff`.

## Synthetic workload and results

C++20, GCC `-O3 -march=x86-64-v3`, twelve threads, 250,000 documents,
53,444 Boolean terms, 90 generated token events/document with skewed term
frequencies, 21,848,046 distinct (term, document) memberships. All variants
produced the same exact count and 64-bit weighted checksum
`32517364094153884`. Synthetic events were generated independently of the
official corpus.

| Kernel representation | Median elapsed, alternating 6+6 runs | Median peak RSS |
|---|---:|---:|
| 8-byte flat hits + 4-byte count/scatter postings | 0.2655 s | 247,896 KiB |
| Inline-eight per-term postings, no scatter | 0.2625 s | 157,128 KiB |

**36.6% lower median peak RSS** and similar median time in this isolated
synthetic membership-construction kernel. Measurements exclude real corpus
tokenization, phrase automaton, final global posting merger, query evaluation,
output, Docker and the scoring CPU. They do not predict the official score.

An earlier run gave 0.638 s for inline-eight while the same executable
usually took 0.25–0.30 s, indicating substantial host-load variation.
Do not infer a reliable speed win from the 0.003 s median difference.

A `std::vector<u32>` per term reduced memory too, but was typically slower
than inline-eight. Inline-one, inline-two and inline-four were also tested;
inline-eight gave a useful speed/memory tradeoff on this synthetic distribution.

## Independent oracle and sanitizers

The 16/24/40-byte small-posting family was compiled with
`-Wall -Wextra -Werror`. The final inline-eight kernel passed **100
independent randomized seeds** using three worker shards, comparing every
term's exact count and every sorted document ID against a separate flat
count/scatter implementation. It also passed
`-fsanitize=address,undefined` with leak detection on the 100-seed oracle.

This verifies the isolated posting representation, not the complete
production solver or the official SHA-256.

## Rejected alternate architectures in this run

1. **64-document bit-sliced Boolean evaluation** eliminates postings but
   evaluated 31,000 queries across 250,000 synthetic documents in ~7.0 s
   single-threaded, too slow relative to the ~0.54 s complete official B run.
2. **Inverted OR-seed candidate queries per block** was exact on 100
   independent randomized corpora but slower (~10.0 s vs 7.2 s on the
   full-sized synthetic case), due to common-term fan-out and random access.
3. **Per-document OR-seed candidate evaluation** was exact but took ~8.7 s
   on 50,000 documents / 8,000 queries, compared with ~0.37 s for 64-bit
   block evaluation.
4. **Track C explicit AVX2 quote parity** was slower than GCC's
   auto-vectorized scalar parity on a synthetic 200 MiB byte stream.
5. **Track D frontier-choice policies** were exact on 150 independently
   checked synthetic local-road queries, but key-minimum search expanded
   more vertices than size balancing. No stable board-relevant win.

## Required next gate

1. Compile the full `track-b/src/main.cpp` in the intended offline image.
2. Run independent query/Unicode/phrase/Boolean/worker-boundary oracles and
   ASan/UBSan on the integrated solver.
3. Run official full seed-1 `docs.txt` and `queries.txt` and compare
   `results.txt` SHA-256 against the manifest.
4. Run alternating whole-process benchmarks versus `425051d`, measure
   peak RSS, time and source-line count; calculate actual scoring coefficient.
5. **Only if all gates pass and K is non-regressing**, promote the B file
   to `speed-demon`. Preserve `425051d` for immediate rollback.

Official board snapshot at 2026-10-31T06:43:41Z: B best 53.122178,
latest 53.024437 (`run-6c7727319a49`), target 69.556.
