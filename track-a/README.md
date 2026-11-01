# Track A — Ingest

Exact zero-dependency Rust implementation of the official Track A (Ingest)
specification.

The log is memory-mapped and cut into line-aligned ranges, one per thread. The
timestamp has a fixed 27-byte layout and the 72-hour span is fixed, so each
line's hour index comes from its day and hour digits alone. A single 64-byte
scan after the timestamp finds the method, endpoint and line ends. Each thread
interns endpoints in a small private table and files a 4-byte record (latency,
local endpoint id, 5xx flag) under its hour.

A serial step merges the endpoint tables and sorts the names, so an endpoint's
global id is its rank in output order. Threads then claim hours in order: each
hour's latencies are counting-sorted by endpoint rank, and three selections per
group produce the percentiles. The exact byte size of each hour's text is known
before formatting, so a serial step computes offsets, sizes the output file and
maps it. Every thread then formats its hours directly into the mapped output.

Dependencies: none.

With another 24 hours: profile the parse scan and the per-hour selection on the
scoring CPU, then tune only changes that improve full-process time exactly.
