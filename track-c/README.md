# Track C: Join

The task, the input files and the exact output format are in the [spec](https://github.com/hackathon-raptors/speed-demon-2026/blob/main/specs/c-join.md).

## Approach

A buffered quote-parity prepass aligns twelve memory-mapped CSV shards to real
record boundaries, including quoted embedded newlines. The byte parser only
materializes names containing escaped quotes; note contents are counted without
copying. Consumed pages are released with `madvise`. BOMs, CRLF/LF, trailing
spaces and integer cents are preserved exactly.

A dense region lookup joins countries, including `_UNMATCHED`. Numeric aggregates
are thread-local. Three-letter country codes use a dense base-26 lookup rather
than hashing an allocated string for every region. Region loading overlaps the
quote prepass. Quote parity uses a byte XOR reduction, and unquoted fields use
`memchr` to find their delimiter.

Names are stored once in a striped exact dictionary with
immutable full strings, lock-free read lookups, insertion mutexes and atomic
country-membership bits. Full byte comparisons resolve hash collisions; each
country's distinct count increments exactly once per decoded name.
An already-set membership bit is read without a write; first membership still
uses atomic `fetch_or`, so racing insertions cannot overcount.
Each stripe owns stable deque entries, avoiding a separate allocation and
bucket-chain deletion for every name.

## Dependencies

None.

## With another 24 hours

Separate parsing and shared-name cache costs on the scoring CPU. Initial phase
profiling found 75-84 ms in their combined stage, versus 10-13 ms loading regions
and 9-11 ms preparing quote-safe boundaries.

The latest local five-run full seed-1 median is 0.098435 s, 14.70 MiB peak RSS.
A same-sandbox alternating comparison measured an 8.3% median time reduction
against the published implementation. This candidate has no official run ID;
80 points has not been reached. See [PERF.md](PERF.md).
