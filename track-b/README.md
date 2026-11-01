# Track B — Index

Exact zero-dependency Rust implementation of the official Track B specification.

Queries are parsed first into a compact exact term dictionary. Corpus shards are
memory-mapped and tokenized with ASCII case folding; non-ASCII bytes remain
separators as required. The immutable dictionary uses SIMD control-byte probing,
CRC32C hashing and full token equality.

Boolean terms are accumulated as 64-document membership words and evaluated
with the required strict left-to-right operator semantics. Phrase queries use
exact token-ID windows around selected anchors; matches are deduplicated per
document and summarized as count, ID sum and first ten IDs. Twelve workers scan
aligned shards and their deterministic summaries are merged in document order.
Transparent huge pages are disabled to avoid board-side memory inflation.

Dependencies: none.

With another 24 hours: reduce per-token dictionary/state traffic and replace
repeated Boolean block work only when an exact full-dataset benchmark improves
the official K axes.
