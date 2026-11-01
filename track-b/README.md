# Track B — Index

Exact zero-dependency Rust implementation of the official Track B specification.

Queries are parsed first into a compact exact term dictionary. Corpus shards are
memory-mapped and tokenized with ASCII case folding; non-ASCII bytes remain
separators as required. The immutable dictionary uses open addressing and
CRC32C over each exact 32-byte lowercase token key. Its 32-bit slots pack a
14-bit hash tag with an 18-bit term ID plus one; full SIMD equality resolves
tag collisions.

Frequent Boolean terms are accumulated as 64-document membership words; OR
makes repeated occurrences idempotent. Less frequent terms use deduplicated
sorted postings. Terms needed only to verify phrases do not create postings.
Boolean queries use the required strict left-to-right operator semantics.
Phrase queries verify exact token sequences around selected anchors; matches
are deduplicated per document and summarized as count, ID sum and first ten IDs.
Twelve workers scan aligned shards and merge deterministic summaries in
document order.
Transparent huge pages are disabled to avoid board-side memory inflation.

Dependencies: none.

With another 24 hours: reduce per-token dictionary/state traffic and replace
repeated Boolean block work only when an exact full-dataset benchmark improves
the official K axes.
