# Track C — Join

Exact zero-dependency Rust implementation of the official hostile-CSV Track C
specification.

The input is memory-mapped and split into quote-safe ranges, preserving embedded
newlines, CRLF/LF, BOM rules, escaped quotes, empty fields and exact byte
semantics. Region IDs use a dense lookup table and numeric aggregation remains
thread-local.

Distinct names use a fixed-capacity concurrent atomic table. Slots carry a
fingerprint plus the first occurrence's offset into the immutable mapping;
fingerprint matches are always resolved by full decoded CSV byte equality.
Per-country membership is tracked with atomic bitsets so races cannot double
count a name. No probabilistic/hash-only equality is used.

Long names are compared byte-for-byte even on hash collisions. The final bytes
are copied behind 64 bytes of zero padding so SIMD loads never run past the
buffer, then parsed last. Quote parity uses BMI2 PDEP. The offline build uses
a PGO training stage on the allowlisted Rust image.

Dependencies: none.

With another 24 hours: profile structural CSV scanning and exact-name cache
misses on the scoring CPU, then tune only exact full-process improvements.
