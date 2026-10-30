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

Only Boolean query terms receive document postings. Sparse sorted IDs promote
to aligned bitmaps when dense; both representations merge exactly across shards.
Boolean operators are applied strictly left to right using mixed sparse/bitmap
operations. Parallel query evaluation produces deterministic input-order results,
each with the count, ID sum and ten smallest matching IDs.
Dense result words use population counts and six weighted bit-position masks
to sum IDs exactly; sparse words still enumerate set bits. The first ten IDs
are always enumerated in ascending order.

Dense bitmaps are allocated only when needed, keeping sparse posting headers
small. Twelve glibc malloc arenas reduce allocation contention across corpus
workers, trading more memory for speed compared with the earlier four-arena
limit. Independent worker posting storage is freed in parallel after its
contents have been merged, before query evaluation.
ASan builds skip this setting because they supply their own allocator.

## Dependencies

None.

## With another 24 hours

Profile corpus token lookup and posting construction/merging on the scoring CPU.
Latest local five-run full seed-1 median: 0.774453 s, 219.74 MiB peak RSS.
The latest same-sandbox alternating tuning measured a 22.8% time reduction
against the preceding implementation, at approximately 22 MiB higher RSS; shared-host
load prevents comparison against older isolated timings. Memory remains an
improvement opportunity. This candidate has no official run ID; see [PERF.md](PERF.md).
