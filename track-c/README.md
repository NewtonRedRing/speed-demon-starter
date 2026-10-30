# Track C: Join

The task, the input files and the exact output format are in the [spec](https://github.com/hackathon-raptors/speed-demon-2026/blob/main/specs/c-join.md).

## Approach

A buffered quote-parity prepass aligns twelve memory-mapped CSV shards to real
record boundaries, including quoted embedded newlines. The byte parser only
materializes names containing escaped quotes; note contents are counted without
copying. Consumed pages are released with `madvise`. BOMs, CRLF/LF, trailing
spaces and integer cents are preserved exactly.

A dense region lookup joins countries, including `_UNMATCHED`. Numeric aggregates
are thread-local. Names are stored once in a striped exact dictionary with
immutable full strings, lock-free read lookups, insertion mutexes and atomic
country-membership bits. Full byte comparisons resolve hash collisions; each
country's distinct count increments exactly once per decoded name.
An already-set membership bit is read without a write; first membership still
uses atomic `fetch_or`, so racing insertions cannot overcount.

## Dependencies

None.

## With another 24 hours

Profile the quote prepass and shared-name cache traffic on the scoring CPU.
Current local full seed-1 median: 0.118340 s, 14.94 MiB peak RSS; no official
run ID is available for this candidate. See [PERF.md](PERF.md).
