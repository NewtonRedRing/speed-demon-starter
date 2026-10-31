# Speed Demon — Maximum Throughput Research (2026-10-31)

**Status:** research, prototypes and experiment specifications. Not a scoring
submission, no new official board run, no 90-point claim. This branch does
not change `track-a` through `track-d` source. All proposed changes
must be tested independently and promoted only by the measured K coefficient.

## Mandatory ground rules

- Public full seed-1 output must match its exact manifest hash; repeated
  lines and values at the limits must remain exact.
- Holdout uses a different seed. No hard-coded catalogue, country list,
  graph labels, query answers, or time/arrival-window assumptions.
- Runtime includes reading, preprocessing, queries and output. Six physical
  P-cores and twelve threads are timed; builds can use permitted offline PGO.
- The actual machine score is `50 * log10(K)`, capped to [0,100],
  with `K=(Tref/T)^0.5 * min(Mref/M,50)^0.2 *
  min(Sref/S,1)^0.15 * (Dref/D)^0.15`.
- Official reference source-line sizes are A=56, B=91, C=108, D=43.
  Reducing format-counted lines helps only at equal algorithmic quality;
  do not sacrifice correctness or judge-readable code for micro-compactness.
- Measure completed process time and **cgroup memory.peak**, not just phase
  microbenchmarks or process RSS. Prewarm input as the harness does.
- Never use a source-level change to claim an improvement until identical
  public-output hashes and alternating full-process measurements pass.

## Updated baseline and why earlier ideas are obsolete

The current `speed-demon` Track A PERF.md already records six-byte records,
deferred counts, hour-local latency arrays, fused SWAR/CRC parsing, AVX2
3/8-element sorting and tight output allocation. A local eight-run comparison
against the submitted A implementation reports 50.484 ms versus 40.346 ms
and a lower cgroup high-water mark. That is **not** an official scoring
measurement. Do **not** start another A branch by implementing these features
again. Test new ideas against the actual current main-branch source.

B already uses exact token Aho-Corasick phrase matching and one-shot
sparse/bitmap postings. C already has striped, read-mostly exact name
membership, quote-safe memory-mapped CSV scanning and dense country lookup.
D already uses packed CSR, wide-weight overflow maps and bidirectional
64-bit radix-Dijkstra. Avoid repeating each track's rejected experiments.

## A — Ingest: target the remaining large-group selection cost

**Candidate A1: co-select three exact quantiles using a hierarchical radix
histogram.** For each group with sufficiently many values:

1. Count high-byte buckets of every uint32 latency.
2. Locate the byte buckets containing ranks ceil(0.50*n), ceil(0.95*n),
   ceil(0.99*n); account for all values in preceding buckets.
3. Refine *only the selected buckets*, with successively lower-order
   byte histograms. Share work if two or three quantiles occupy one bucket.
4. Return actual uint32 input values, without rounding or approximation.

Correctness follows from lexicographic unsigned-byte ordering of
uint32 integers: all values in lower high-byte buckets precede all values in
higher buckets. Within a bucket, recurse on the next byte. Subtracting
prefix counts recovers the exact nearest-rank value, with duplicates counted.

The performance comparison must use 64/128/256/512/1024/4096+ group sizes,
full-width uint32 extremes, identical values and heavily concentrated
near-zero distributions. Only dispatch to radix when the median of entire
Track A improves: small groups are already exceptionally optimized.
The in-place American-flag scheme and multi-line parsing interleaving remain
separate exploratory candidates, not pre-approved changes.

**Reject:** re-implementing six-byte records, generic dataframe engines,
approximate quantile sketches, or a precomputed public-endpoint dictionary.

## B — Index: replace most full-stream phrase automaton work

**Candidate B1: query-term-anchored exact phrase verification.** This is the
highest potential architectural gain. For each phrase query `q[0..k-1]`,
choose an anchor term `q[j]`. Selection can use *query-term frequency*
or a cheap document sample to prefer rare terms. The selected anchor is
a performance choice only; it never changes what constitutes a match.

Construct an inverted mapping:
`anchor_word_id -> [(phrase_query_id, anchor_offset), ...]`.
While scanning each document, collect its exact tokenizer output as a
short-lived vector of word IDs (including the `missing` sentinel). For
each occurrence of word `w` at position `p`, examine candidate phrases
anchored on `w`; compute `start=p-anchor_offset`, bounds-check, then
compare every query token to `document_words[start..start+k]`. Only verified
matches count, and each phrase/document pair is stamped once. At document
end the token vector can be reused.

**Exactness proof:** Every occurrence of a phrase `q` at `start` contains
the selected anchor `q[j]` at `start+j`; the verification therefore
examines that occurrence. Whole-token comparison prevents false positives,
and document stamps prevent double counts for duplicates/repeated words.
Tokenization and byte-lowercasing are *unchanged*. No term popularity
assumption is needed for correctness.

The official generator reports about 98.2% of phrase queries contain a
rare anchor (document frequency <= N/1000). A preliminary independently
checked synthetic prototype used 1,300 documents, 195,000 document tokens
and 850 phrases; all results matched an exhaustive independent phrase oracle.
It inspected 19,725 candidate phrase positions, about 0.101 candidate checks
per document token. This is not an end-to-end C++ performance result and
should not be described as an AC speedup.

**Candidate B2 hybrid:** retain Aho-Corasick for phrases with very frequent
chosen anchors; route the others to the document-token verifier. Select the
cutover using measured frequency distributions, not heuristically assuming
all rare terms remain rare on holdout. Compare tokenization+hashing,
automaton transitions, candidate verifications and allocator operations.
The scan itself is currently the dominant B stage; pure query-format tuning
will not deliver a transformative speedup.

**Reject:** using Unicode text case folding, dropping duplicate document
semantics, storing hash-only token IDs without resolving collisions.

## C — Join: exact, contention-aware name deduplication

The current cost center is the shared name table plus CSV scanning.
Research **two alternative builds** rather than mixing them blindly:

- C1: compact stable entries with `(hash, length, inline first 16 bytes)`
  and a pointer to exact remaining bytes; compare the prefix before following
  the string. This may reduce cache misses on unsuccessful probes but
  increases entry footprint. Preserve full-byte equality on collisions.
- C2: two-level sharding: each worker maintains a small bounded *exact*
  cache of hot `(name, country)` pairs; only first misses reach the striped
  dictionary. The previously tested 1024-entry cache *lost performance*,
  so try this only with a different cost model, cache geometry and
  controlled per-lookup hit-rate measurement. Otherwise reject it.

For parser work, consider the **SIMD string-search / scalar state-machine
hybrid** exemplified by `medialab/simd-csv`: identify comma/quote/LF bytes
with register masks but update quote state only on structural characters.
Do not replace the exact CSV grammar with a quote-parity-only parser.
The original input includes escaped quotes, embedded newlines, mid-file BOMs
and different newline styles. Benchmark quote-heavy records separately;
vectorization can regress when structural characters are dense.

**Reject:** pure fingerprint-based distinct counts; common-name
normalization; dropping one of the two CSV line-break variants; global
radix-sort dedup without proving its extra memory and bytes moved win.

## D — Traverse: local-query short paths, exact 64-bit distances

The graph has 1.74M nodes, 5M directed edges, 2000 queries; source IDs
are randomly relabeled. Query targets are mostly local by shortest-path
settled rank. Directedness, zero edges and uint32-wide weights preclude
heuristic guesses or truncation.

**Candidate D1: conditional forward-first warmup.** Run exact forward
Dijkstra for at most W settled vertices. If it settles the target,
return the exact shortest-path distance. Otherwise initialize the
reverse search and *continue* from the forward frontier, reusing its
distances. The standard bidirectional minimum-frontier bound then
guarantees exactness. Try W=0/4/8/16/32 and benchmark both visited
edges and whole-process elapsed time.

A small independent Python implementation matched ordinary single-source
Dijkstra on 303 synthetic queries involving zero and large weights.
However synthetic timing and expansion counts for W=4/16/32 were mixed;
there is **no reliable net performance win**. Do not promote without
a different generator seed and full official data comparison.

**Candidate D2: query graph-construction overlap.** Parse the small query
file while degrees/edges are being constructed, classify trivial cases
such as source==target and zero-in/out-degree, then prepare workloads.
Only reduce total time if the additional synchronization or scheduling
cost is lower than saved query work; do not assume it will be.

**Candidate D3: search-frontier scheduling by measured relaxation cost**,
comparing radix-queue size, current frontier minimum and cumulative
degree/edge work as side-selection heuristics. Any side can be expanded
if each directional queue remains internally Dijkstra-ordered and the
exact global termination bound is retained. Validate large and zero
edge weights, duplicates, unreachable pairs and graph islands.

Contraction hierarchies, customizable route planning, landmark A* and
hub labeling are genuine options, but their **within-run construction**
must beat the existing ~250 ms local query stage; published microsecond
query times after *offline* preprocessing do not establish that.

## Quantitative 90-point feasibility check

Using the **reference axes** A(32.05 s,228 MB,56 lines),
B(56.35 s,2200 MB,91), C(14.44 s,367 MB,108),
D(72.69 s,893 MB,43), one runtime dependency, and *illustrative*
candidate resources A(36 MB,350 lines), B(280 MB,600),
C(22 MB,400), D(131 MB,365), the time required for
**90 machine points** is approximately:

| Track | Illustrative peak M | S lines | T for 90 points |
|---|---:|---:|---:|
| A | 36 MB | 350 | 9.7 ms |
| B | 280 MB | 600 | 18.3 ms |
| C | 22 MB | 400 | 7.5 ms |
| D | 131 MB | 365 | 20.7 ms |

These are algebraic scenarios, **not predictions or measured targets**.
The largest unknown is the actual scored runtime/memory/source size
on the official machine; a design can trade among axes.

## Experimental gate and ordering

1. Run the exact pinned official image and score on current `speed-demon`
   first. If the board is stale, state it. Preserve the current run ID.
2. Prioritize **B1/B2** because they change asymptotic work on 36M tokens.
3. Evaluate **A1** on actual hot group-size histograms, not uniform arrays.
4. Evaluate **C1** with compulsory full quoted/escaped-input oracle.
5. Evaluate **D1/D3** with multiple real generator seeds and all-pairs tiny
   graph oracle. Record graph-build and query-stage timing separately.
6. Capture PGO, CPU branch/cache profiles, peak cgroup memory and three
   or more alternating full-process repetitions. Prefer the full official
   score improvement, not isolated instruction counts.
7. Merge a candidate only when exact SHA-256, offline build, sanitizers
   and full score pass. Document rejected trials in each Track PERF.md.

## Relevant external research

- Official rules, workload and scoring: https://speeddemon.dev/
- Spec and generator: https://github.com/hackathon-raptors/speed-demon-2026
- Rare-byte prefilter and packed multi-pattern matching:
  https://docs.rs/crate/aho-corasick/latest/source/DESIGN.md
- SIMD-delimiter and scalar state-machine hybrid CSV:
  https://github.com/medialab/simd-csv
- RoutingKit contraction hierarchy (build time must be counted):
  https://github.com/RoutingKit/RoutingKit/blob/master/doc/ContractionHierarchy.md
- GraphHopper LM/ALT and CH distinction:
  https://github.com/graphhopper/graphhopper/blob/master/docs/core/routing.md
- Exact reference axes:
  https://github.com/hackathon-raptors/speed-demon-2026/blob/main/harness/reference_axes.json
