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
count a name. No probabilistic/hash-only equality is used. Transparent huge
pages are disabled.

Dependencies: none.

With another 24 hours: profile structural CSV scanning and exact-name cache
misses on the scoring CPU, then tune only exact full-process improvements.

Long names are compared byte-for-byte even on hash collisions. The input
mapping provides a spare zero page so the final records are parsed without a
temporary copy. Standard Rust source and the offline PGO image are retained.

Board run `run-0b1d0e65fdba` showed higher peak memory after the zero-copy
mapping experiment. The original copied/padded tail is reinstated; long names
still compare exactly and quote parity uses BMI2 PDEP. The previously best
single-seed PGO build is restored for another scored comparison.
