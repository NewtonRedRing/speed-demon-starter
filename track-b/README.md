# Track B: Index

The task, the input files and the exact output format are in the [spec](https://github.com/hackathon-raptors/speed-demon-2026/blob/main/specs/b-index.md).

## Approach

An exact token-level Aho-Corasick automaton matches all phrase queries while
twelve aligned corpus shards are scanned. Failure/output links retain overlapping,
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

Dense bitmaps are allocated only when needed, keeping sparse posting headers
small. Four glibc malloc arenas limit fragmentation from parallel allocations;
ASan builds skip this setting because they supply their own allocator.

## Dependencies

None.

## With another 24 hours

Profile corpus token lookup and posting construction/merging on the scoring CPU.
Current local full seed-1 median: 0.834935 s, 202.97 MiB peak RSS. Memory remains
an important improvement opportunity. No official run ID is available for this
candidate; see [PERF.md](PERF.md).
