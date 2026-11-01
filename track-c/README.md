# Track C — Join

Exact zero-dependency Rust implementation of the official hostile-CSV Track C
specification.

The input is memory-mapped and split into quote-safe ranges. The parser uses
the specification's field grammar: nonempty names and notes are quoted, while
IDs, region IDs and amounts are unquoted. AVX2 quote searches skip escaped pairs;
commas and newlines inside quotes remain data. Empty quoted and unquoted fields,
CRLF/LF and record-start BOMs retain their exact semantics. Region IDs use a
dense lookup table and numeric aggregation remains thread-local.

Distinct names use a fixed-capacity concurrent atomic table. Length and the
first/last eight raw bytes select a candidate bucket, never establish equality.
Slots store short names exactly; longer names store their first sixteen bytes
and compare the remaining bytes in an arena. CSV escaping is canonical for
nonempty names, so raw equality is exact decoded-value equality; both encodings
of an empty name use the same zero-length key.
Per-country membership is tracked with atomic bitsets so races cannot double
count a name. No probabilistic/hash-only equality is used.

Long names are compared byte-for-byte even on hash collisions. The final bytes
are copied behind 64 bytes of zero padding so SIMD loads never run past the
buffer, then parsed last. Transparent huge pages are disabled for sparse tables.
The offline build uses a PGO training stage on the allowlisted Rust image,
with the unmodified official generator and a separate training seed.

Dependencies: none.

With another 24 hours: profile structural CSV scanning and exact-name cache
misses on the scoring CPU, then tune only exact full-process improvements.
