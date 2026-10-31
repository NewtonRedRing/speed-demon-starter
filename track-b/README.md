# Track B: Index

The task, the input files and the exact output format are in the [spec](https://github.com/hackathon-raptors/speed-demon-2026/blob/main/specs/b-index.md).

## Approach

An exact token-level Aho-Corasick automaton matches all phrase queries while
twelve aligned memory-mapped corpus shards are scanned without copying document
lines. Consumed full pages are released in 1 MiB batches. Failure/output links retain overlapping,
repeated and duplicate phrases, with matches deduplicated per document. Each
phrase retains only its exact count, sum and first ten document IDs; aligned
shards merge these summaries in document order. No positional postings or full
phrase-result lists are stored. ASCII-only tokenization treats non-ASCII bytes
as separators.

Only Boolean query terms record document memberships. Each worker appends
deduplicated term/document hits to one contiguous vector, counts terms, then
prefix-scatters IDs into worker-owned contiguous posting slices. Input document
order makes each slice sorted without comparison sorting.

Parallel tasks claim batches of 32 terms. Each task reads all worker slices and
constructs its term's final sparse vector or dense bitmap once, based on global
density. Dense terms do not allocate temporary global ID vectors. Each posting
has one writer; there are no per-hit atomics or cross-worker scatter writes.
Boolean operators are applied strictly left to right using mixed sparse/bitmap
operations. Parallel query evaluation produces deterministic input-order results,
each with the count, ID sum and ten smallest matching IDs.
Dense result words use population counts and six weighted bit-position masks
to sum IDs exactly; sparse words still enumerate set bits. The first ten IDs
are always enumerated in ascending order.

Dense bitmaps are allocated only when needed. Twelve glibc malloc arenas reduce
contention during parallel allocations. Flat hit buffers are freed after local
partitioning; worker posting slices are freed in parallel after final postings
have been built, before query evaluation.
ASan builds skip this setting because they supply their own allocator.

## Dependencies

None.

## With another 24 hours

Profile the remaining token lookup and phrase-transition work on the scoring
CPU. The public input contains about 36.3 million tokens and 21.1 million
Boolean memberships, substantially more operations than its document count.

Latest local five-run full seed-1 median: 0.509163 s, 278.18 MiB peak RSS.
The latest eight-run alternating comparison measured 0.786195 s before versus
0.625108 s after: 20.5% lower time, with approximately 61 MiB higher peak RSS.
This is a new gain against the previously published implementation, not a
cross-batch comparison with older isolated timings. This candidate has no
official run ID; see [PERF.md](PERF.md).
