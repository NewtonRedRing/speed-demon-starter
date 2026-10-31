# Extreme exact architectures after score-gap analysis

Research date: October 31, 2026. This is a **hypothesis and prototype report**,
not an official competition result. It extends the earlier 128-candidate screen.

## Target math: why small tweaks are insufficient

Official machine points = `50 * log10(K)` and
`K = (Tref/T)^0.5 * min(Mref/M, 50)^0.2 *
min(Sref/S, 1)^0.15 * (Dref/D)^0.15`.

Using the last verified board values (which predate commit `0334031`):

| Track | Previous board points | Target | T on board | T needed if all other axes fixed | Speedup |
|---|---:|---:|---:|---:|---:|
| A | 75.373 | 85.274 | 36.870 ms | 14.813 ms | 2.489x |
| B | 53.122 | 69.556 | 537.941 ms | 118.408 ms | 4.543x |
| C | 65.531 | 76.840 | 84.922 ms | 29.968 ms | 2.834x |
| D | 58.797 | 72.925 | 418.953 ms | 114.037 ms | 3.674x |

A twofold improvement in memory charge, all else fixed, reduces the required
runtime factor by `2^0.4 ≈ 1.320`. Source-line and dependency terms still
count. **Never use a synthetic microbenchmark as a board score.**

Raw feed: https://s3.amazonaws.com/raptors.dev/speeddemon.dev/2026/board/board.json

## A — one-pass hot-group streaming, not another percentile variant

**Proposed extreme approach:** 12 independently scanned input stripes,
software-pipelined two/four-record SWAR row parsers, a compact hot-endpoint
front cache and a chunked hour-bucket record arena with directly assigned
per-worker group IDs. Handle the cold catalogue separately. Aim to eliminate
the second per-record remapping pass while preserving exact bytewise
endpoint registration and future-seed independence. Keep a compact
per-group latency list and exact nearest-rank selection.

The biggest risk: the current solver is already strongly optimized; global
per-endpoint ID registration must not introduce contention or require
copying 3.1M rows again.

**Rejected in local isolated testing:** shared three-rank quickselect over
large groups measured about 89.1 ms against 78.0 ms for the existing
three `nth_element` strategy on a 10k-group synthetic workload
(different alternating run measurements can vary). This is not a
whole-process comparison. Do not treat multi-select as a breakthrough
without actual group-size phase profiles. The most promising new work is
in the 239MB input parser/record dispatch rather than large-group sorting.

**Go/no-go:** exact 3.1M-row SHA and at least 20% lower full-process time
without raising cgroup peak; benchmark source code line penalty.

## B — remove phrase automaton from document-token hot path

A former hybrid (rare-word anchors plus Aho–Corasick for the remaining
common phrases) was just 1.02x faster in an isolated synthetic phrase
scan because AC still advances on *every word*.

**New exact architecture: RARE-ONE + COMMON-QGRAM.**

Parse queries first. Select one rare anchor word for phrase queries with
low query-term frequency. Group the remaining common-only phrases by
their rarest **adjacent pair**, stored as one exact 64-bit key
`(uint64_t(word_id1)<<32) | word_id2`. For every document:

1. Emit exact tokenizer word IDs into a reusable, short-lived per-document
   array. Unknown words are a distinguished sentinel.
2. Probe the one-word anchor index at each word and the two-word index at
   each adjacent pair. Only query-index hits cause phrase verification.
3. Bounds-check candidate start positions and verify **every phrase word**
   against the document-token array; stamp once per phrase/document pair.
4. In parallel, collect the original exact Boolean memberships. Preserve
   left-associative Boolean operators and first-ten IDs.

No probabilistic token filter ever determines correctness. Each phrase has
one selected trigger, guaranteed to exist in every real occurrence, and
full token comparison prevents false positives. Two-word keys are exact
because two 32-bit IDs fit into one 64-bit integer.

**Local C++ prototype measurements, phrase stage only** (10,000 synthetic
phrases; 180 tokens/document; AC comparison uses identical input):
- 40k documents: anchored+qgram 0.235s vs AC 0.333s, **1.42x**;
- 30k documents, seeds 2026/719/349: approximate improvements
  **1.71x / 1.30x / 1.33x**;
- Every phrase's last matching document matched the AC oracle in each test.

These measurements EXCLUDE text tokenization, query parsing, Boolean
postings, exact output formatting, overall startup and official cgroup memory.
No reported official speedup and no evidence of a 4.5x improvement yet.
A more efficient direct token-id parser and lower-memory Boolean postings
are necessary to approach the target.

**Go/no-go:** build in a separate branch, run official full SHA and multiple
generator-seed oracles, profile the 36.3M-token scanner and phrase stage, and
compare total wall time. Reject if an unexpected common qgram causes heavy
candidate fan-out. Avoid repeated per-document allocations.

## C — compact exact-name arena, fused structural scanning

A synthetic 3.32M-event / 100k-distinct-name / 240-country
single-threaded membership experiment measured a `string_view`
dictionary at **467.7 ms** vs allocating `std::string` keys at
**583.4 ms** (1.25x) with equal 3,101,489 country/name events.
This is a kernel-only result, not whole-Track C performance.

**Critical mmap caveat:** the current parser calls `MADV_DONTNEED`
on consumed file pages; retaining views of older names in the dictionary
can cause repeated page faults. Therefore do NOT simply swap all
dictionary keys to input-backed `string_view` and claim a speedup.

**Proposed exact architecture:** each shared hash stripe owns a compact
append-only name arena with stable 64KB/1MB chunks. Dictionary entries
contain a length, hash, arena offset/pointer, next-entry index and four
64-bit country-membership words. New distinct names are copied ONCE
into the stripe's sequential arena; common name lookups compare in-place
against its stable bytes. Escape-decoded and plain names use the same
byte-exact representation. Keep lock-free lookups and the one-time atomic
membership bit set. Pair this with a two-speed SIMD structural mask parser:
quote-heavy fields stay on an optimized quote-aware scalar path. Do not
normalize UTF-8, whitespace, BOM or any quoted text.

**Reject** all-record sorting and owner queues until measured data
movement and >50MB temporary memory costs are justified.

**Go/no-go:** exact full hostile CSV output, preferably >=2x reduction
in parser+dictionary time, memory. Evaluate `madvise` side effects and
the source-line cost of an AVX2 parser.

## D — query-terminal-aware min-plus vertex elimination

All 2,000 query pairs are available as input. Read these first and protect
every unique source/target as a **terminal**. Construct the directed graph,
then eliminate nonterminal low-degree vertices using exact min-plus
shortcut composition:

For each removable vertex v and every incoming (p,v,w1) and outgoing
(v,s,w2), introduce/relax an arc (p,s,w1+w2), remove v and its edges.
Use 64-bit shortcut sums, preserve directional asymmetry, parallel-edge
minimum, zero edges, disconnected components and every terminal.
Only contract if `indegree(v) * outdegree(v)` is small (e.g. <=4 or <=9)
and the extra arc/heap work is compensated.

**Why exact:** any path between protected terminals that visits v can
replace its two-arc subpath with its min-plus shortcut; every created
shortcut expands to a real original path. With nonnegative weights,
cycles do not improve shortest paths.

**Independent small-scale Python verification:**
- 250 random directed graphs, 12,250 terminal-pair checks vs independent
  Dijkstra, including 0 and >UINT32_MAX weights: exact;
- synthetic 100x100 lattice: 3,273 of 10,000 nodes removed, 28,998
  directed arcs -> 22,286; query-only 400-pair Dijkstra time
  2.547s -> 2.024s (1.26x), excluding preprocessing;
- synthetic 70x70 lattice, protecting 84 terminals:
  degree product<=9 eliminated 2,871 of 4,900 nodes and
  14,208 -> 9,620 arcs; 400 query-only paths 1.264s ->
  0.819s (1.54x), excluding preprocessing.

The large original main graph may have different low-degree and
hub/shortcut distributions. **There is NO full-input D result yet.**
Full min-plus elimination can become worse because it adds shortcuts
and preprocessing; an adaptive threshold and limited rounds are essential.
Queries are local, so compare against original bidirectional radix Dijkstra,
not a plain one-direction baseline.

**Potential stronger hybrid:** bounded min-plus elimination of low-degree
nonterminals + one-pass compact graph construction + radix/Dial hybrid
bidirectional search on the contracted overlay. Stop contraction when
its estimated preprocessing cost exceeds expected saved query work.

## Experiment and publication constraints

1. The competition times **graph preprocessing and output** too.
2. Only the official pinned AVX2 6-P-core CPU and exact public seed-1 SHA
   can determine a leaderboard gain; the holdout seed changes vocabulary,
   values, IDs, queries and layouts.
3. Prefer a separate branch until the independent exact oracle passes.
4. When pushing a candidate to `speed-demon`, keep an exact rollback SHA,
   watch the board result and revert failures promptly.
5. None of these designs proves 85.274 / 69.556 / 76.840 / 72.925.

## Relevant references

- Official specs and generators: https://github.com/hackathon-raptors/speed-demon-2026
- Modern SIMD CSV modes: https://github.com/medialab/simd-csv
- Exact multi-pattern seed-and-verify background:
  https://docs.rs/crate/aho-corasick/latest/source/DESIGN.md
- Query-aware vertex elimination is a restricted min-plus form of
  shortest-path contraction, with the usual CH preprocessing-vs-query
  tradeoff: https://github.com/RoutingKit/RoutingKit
