# Track B: exact adaptive hot-term bitmaps (proposal, not validated)

October 31, 2026. The board confirmed the new B short-token lookup
change regressed from 53.122178 to 52.902629 points. The former exact
version was restored on the submission branch; do not treat the hot-bitset
proposal as a scored improvement.

## Why this targets the dominant cost

The repository's full-data profiling records 36.3M tokens, 28.7M known
query-token occurrences and 21.1M distinct (Boolean term, document)
memberships across 53,444 Boolean query terms.

The current `Local::hits` uses an 8-byte `{u32 term,u32 document}`
for each distinct membership, then builds a second 4-byte document
posting array by prefix/scatter before constructing the final postings.
At 21.1M hits, the first two arrays alone are about 169 MB + 84 MB
of payload at their overlap, ignoring capacities and metadata. The
official board reports 300,158,976 bytes peak for the known-good
version and 301,297,664 bytes for the recent slower variant.

## Proposed exact redesign

Choose a bounded number of hot Boolean terms from query-term occurrence
frequency or an initial pilot segment of the document corpus. **This
choice affects performance only; it never changes membership semantics.**
Keep `hot_slot[boolean_id]` (or sentinel) and per-worker dense bitmaps
for hot terms, allocated only for that worker's aligned document range.
For cold terms, retain the existing flat `Local::hits` and
prefix/scatter posting construction.

During token scan, when `last[boolean_id] != document`, set the hot
bit directly or append the cold hit. At finalization, concatenate each
worker's hot bitmap segments into the term's final bitmap (or merge
word-aligned segments with OR for boundaries), and build cold postings
with the current exact method. All subsequent Boolean evaluation
remains unchanged.

A pilot segment is optional. If used, collect its memberships in the
existing exact cold-hit representation, select hot terms, and merge
those initial postings into the final hot bitmap; never discard pilot
matches. An alternative is to choose hot terms deterministically from
query frequencies before corpus scanning, avoiding a second pass.

Because document IDs are dense 0..N-1 and each worker has a disjoint
range of complete lines, 256 hot terms need only
`256 * ceil(N/64) * 8` bytes, ~8 MB at N=250,000, rather than
one 8-byte hit per matching (term,doc). A hot bitmap is also the final
posting, so there is no 8-byte->4-byte temporary duplication for those
terms.

**Correctness:** Every membership bit represents the exact same
Boolean term/document pair. Repeated words in one document set the
same bit once; no approximate filter decides membership. The phrase
matcher remains untouched. The hot/cold partition is invisible to the
query evaluator, which continues exact left-associative Boolean
operations.

## What must be measured

- Actual hot-term share of the 21.1M memberships, rather than assuming
  Zipf frequency alone predicts the distribution.
- End-to-end full-data runtime including per-token hot-slot checks,
  cache pressure from dense bitmap writes, allocation, and finalization.
- cgroup peak memory, not only heap allocations.
- Official full public SHA-256 and independent tiny/Unicode/duplicate
  Boolean oracles, followed by the scoring board.
- Hidden-seed robustness: no fixed term identities, counts or outputs.

At constant runtime, reducing 300 MB peak to 80 MB would add about
5.74 machine points under the official 0.20 memory exponent and
50*log10(K) scoring. That still leaves about 2.68x runtime improvement
to reach 69.556 points. This is a **scenario**, not a measured result.

## Evidence

- Official live feed: https://s3.amazonaws.com/raptors.dev/speeddemon.dev/2026/board/board.json
- Existing source: track-b/src/main.cpp
- Existing profiling: track-b/PERF.md
