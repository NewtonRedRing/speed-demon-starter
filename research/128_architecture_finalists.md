# Four architecture finalists — exactness, cost model and acceptance tests

Read [the 128-candidate screen](128_algorithm_screen.md) and
[the full CSV matrix](128_algorithm_screen.csv) before implementing.
**These are architectural hypotheses, not measured speed rankings.**

## Baseline and scoring model

The fixed competition has a timed native process, source-size and memory
penalties, and an unseen holdout seed. At the time of analysis the
`speed-demon` branch uses:
- **A:** 12-worker fused SWAR/CRC parser, 6-byte records, hour-local scatter,
  AVX2 exact small-group sorting, three `nth_element` selections above 32.
- **B:** 12-worker token-Aho–Corasick + flat Boolean hit arrays, adaptive
  sparse/bitmap postings and exact left-associative query processing.
- **C:** quote-safe CSV mmap, scalar `memchr` parsing, shared striped exact
  name dictionary with lock-free reads and per-name 240-country bits.
- **D:** two buffered graph passes, compact bidirectional CSR, exact
  64-bit radix-queue bidirectional Dijkstra with sparse stamped distances.

Scoring is multiplicative:
`K = (Tref/T)^0.50 * min(Mref/M,50)^0.20 *
min(Sref/S,1)^0.15 * (Dref/D)^0.15`.
Doubling speed with the other axes fixed earns **7.526 machine points**,
while halving memory earns **3.010 points**. Neither is sufficient
evidence on its own to award 90 points.

## A: three-rank exact multiselection, adaptively dispatched

### Proposed algorithm
For groups above a measured threshold, calculate ranks
`r=(q*n+99)//100`, for q = 50,95,99. Construct a high-byte
histogram over all 32-bit unsigned latencies. Locate which histogram
bucket contains each rank and subtract preceding bucket populations.
For each *distinct selected* bucket, scan only its values and build
the next-byte histogram. Continue until all four bytes have been
resolved; if two ranks share a bucket, reuse the same histogram.

An alternative is multi-rank quickselect using shared 3-way
(`<pivot`, `==pivot`, `>pivot`) partitions. Recursively process only
subranges containing one of the three ranks, with fallback to an exact
worst-case pivot selector if necessary. This avoids repeated selection
scans and handles identical values efficiently.

### Exactness
Ascending unsigned byte lexicographic order equals unsigned uint32
order. Rank counts include every duplicate, so the selected value
equals the oracle's `sorted()[rank-1]`. For the quickselect design,
strict partition invariants preserve all candidate rank positions.

### Critical limitation
Your distribution already has median group size around 6 and ~90% of
groups <=32; therefore faster large-group quantiles may improve the
non-parser tail only. Prioritize only if a phase timer shows sufficient
time spent in >32-element groups. Small groups should retain existing
AVX2 path. A two-stream independent row parser may ultimately offer
more end-to-end benefit, but currently has a higher implementation risk.

### Measurement
Randomized bins with 1,2,3,8,9,32,33,64,99,100,255,256,512,1k,4k
and the largest official group; adversarial 0/UINT32_MAX/duplicates,
ascending and descending; full SHA-256 and >=8 alternating official
process runs, including cgroup memory and source lines.

## B: hybrid query-anchor phrase verification

### Proposed algorithm
Parse query terms first. Assign each phrase exactly one anchor position
j. Initially estimate document frequency using query vocabulary/cheap
sample; choose low-frequency anchors. Retain Aho–Corasick only for
phrases whose chosen anchor is frequent, and index rare anchors as
`word_id -> [(phrase_id,j)]`.

Scan the corpus once per worker while continuing to record Boolean
memberships as today. Reuse a short-lived stream of token IDs for the
*current document*; unknown words are a distinct `missing` sentinel,
not a valid query term. At anchor occurrence position p, verify the
exact full phrase `q` at `start=p-j`, waiting for enough lookahead.
Bound pending verification by phrase length (<=8). For each phrase
and document, stamp results once even if multiple occurrences or
duplicate query lines exist; preserve first-ten document IDs.

### Exactness
Any real occurrence of a phrase contains its selected token at offset j,
so its start will be proposed. Every proposed candidate undergoes
complete position-by-position equality against query tokens, which
rejects false positives and handles repeated words. Document stamping
enforces once-per-document counts. If AC is used for the common subset,
each phrase must be assigned to exactly one matching engine, not both.

### Where speed might disappear
There are 19,000 phrases and ~36.3M tokens. The official generator
says ~98.2% of phrases contain a rare token, but rarity is not known
for the hidden seed until at least sampled. If common anchors fire
frequently, comparisons/dispatch can cost more than the existing AC.
The tokenizer and Boolean hit collection remain large fixed costs.
Using token IDs rather than bytes is exact only because the tokenizer's
query dictionary resolves hash collisions with *full-byte equality*.

### Measurement
Instrument per-token AC transitions, candidate verifications, verified
matches, document-token-buffer bytes, and worker wait time. Compare
(1) all AC, (2) all anchored, (3) anchored+AC hybrid. Reuse 15+ independent
phrase/Unicode/repeat/left-associative Boolean oracles, full seed-1 hash,
multiple new generator seeds and cgroup peak.

## C: owner-local exact-name deduplication vs current lock-free reads

### Proposed algorithm
*Do not* blindly sort all 3.32M pairs or allocate 240 bitmap columns
per worker. The current name pool is ~100,000 unique values and the
existing dictionary is read-mostly. A theoretically better variant
parses names and aggregates numeric columns in worker-local storage,
then routes `(name_hash, country, name_span)` events to 64 hash owners.
Each owner deduplicates names using exact byte equality, with a
4x64-bit membership mask per name, and returns exact country distinct
counts. Only owner threads mutate their dictionary buckets; no shared
per-event membership atomics are required.

The event queue adds at least ~16–24 bytes per input row (53–80MB before
overhead) unless batches are compressed or source spans are reused.
This may outweigh all lock savings, especially since the current
`Stripe::insert` is already a lock-free **read** for existing names.
An alternative is worker-local compact exact name IDs with sparse
per-country masks, then a second exact merge. **No design is guaranteed
faster**; prefer conservative optimization of the existing dictionary
if full source profiling shows little synchronization time.

### CSV parser
Use a two-speed tokenizer: amortized structural masks for long
unquoted spans, optimized quote search for quote-heavy fields. The
medialab/simd-csv implementation specifically reports that a single
wide-register approach can lose in the quoted case; its quoted-heavy
benchmark gains are only modest. Keep the current exact state machine
for escaped quotes, BOMs at legal record boundaries, CRLF/LF, and embedded
LF in notes. No field may be lowercased or normalized.

### Exactness
Names must compare byte-for-byte *after* `""` unescaping; `""` and
empty unquoted fields denote one empty name; trailing spaces matter.
The country bit must be set once for each distinct name/country
pair, even when 12 parsers race. Region IDs outside the lookup map to
`_UNMATCHED`, all amounts are signed integer cents.

### Measurement
Separate CSV structure scan, decoded-name creation, hashed lookup,
country membership and parser time under pinned CPU. Compare existing
stripes vs owner-queue vs local-merge using 3.32M-record full workload.
Require an end-to-end time improvement large enough to compensate
for any 2–4x increase in memory; local microbenchmarks are not proof.

## D: short-forward/dual-frontier exact adaptive shortest paths

### Proposed algorithm
The generator samples query targets by shortest-path Dijkstra rank;
half of normal queries have a rank at most ~512. Start a query with
a single exact forward Dijkstra frontier for W settled vertices.
If the target settles, answer instantly; otherwise initialize the
reverse frontier and continue *the same forward search*, keeping all
forward distances and queue entries. Use the existing exact global
bidirectional termination criterion. Test W in
`{0,4,16,64,256,512,1024}`; do not assume 4/16 is optimal.

Also test queue-size versus expected relaxation-cost side selection,
and a bounded Dial queue for ordinary small integer weights with a
globally ordered radix/heap overflow tier for exceptional full-width
weights. The hybrid queue must never pop a larger distance ahead
of a smaller one. A one-pass graph parser storing compact raw edges
may avoid the second 92MB parse, but adds ~60MB raw edge storage and
must be scored for peak cgroup memory.

### Exactness
Any early answer occurs only when the forward target is *settled*;
mere discovery is not sufficient. The frontier transition retains
all discovered distances, and a correct two-sided lower bound permits
termination. Use uint64 distances and maintain directedness, parallel
edges, zero weights, disconnected islands, and distances >UINT32_MAX.
Do not use randomized node IDs as geometric coordinates.

### Preprocessing trap
Full contraction hierarchies and ALT can have excellent queries, but
all preprocessing is timed. RoutingKit explicitly emphasizes CH
preprocessing as an expensive phase. The current ~0.4s local whole
solver / ~0.25s query stage offers little amortization headroom for a
large hierarchy: only accept after whole-run measurement across seeds.

### Measurement
Verify every tiny random graph by all-pairs independent Dijkstra.
Track query-rank distribution, settled nodes, scanned edges, queue
push/pop, hash probes, and graph-construction stages. Compare W=0
against W>0 across many controlled runs and record memory/line cost.

## Prioritization / checkpoints

1. **B anchored/hybrid:** largest opportunity to skip a repeated algorithmic
   stage, but correctness and candidate fan-out are critical.
2. **D query-adaptive exact search:** workload-specific upside; test simple
   W-first strategies before queue redesign or preprocessing.
3. **A multi-rank selection:** easiest exact prototype, uncertain
   *whole-process* gain because most groups are tiny.
4. **C dictionary+CSV:** hardest to beat because current shared name
   lookups are already mostly lock-free; reject owner-queue if memory
   and byte traffic dominate.

All outcomes are **research hypotheses**, not guaranteed first place.
Only full public output hash + repeated official-K improvement + hidden-seed
robustness justify merging any candidate into `speed-demon`.

## Research origins
- Official frozen specification, score and harness:
  https://speeddemon.dev/
- SIMD CSV quoted/unquoted search:
  https://github.com/medialab/simd-csv
- Aho–Corasick rare-byte prefilter architecture:
  https://docs.rs/crate/aho-corasick/latest/source/DESIGN.md
- CH preprocessing-versus-query tradeoff:
  https://github.com/RoutingKit/RoutingKit/blob/master/doc/ContractionHierarchy.md
- Parallel CH construction (2025):
  https://doi.org/10.1145/3721145.3725744
