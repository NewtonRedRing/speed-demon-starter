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

## Why this computational model

All queries are available before index construction, so indexing only
query-relevant tokens eliminates unnecessary postings. The scanner is fused
with document membership creation: phrase-only terms need token-position
verification but no Boolean document posting. Exact doc IDs remain sorted
within each posting list, bitmap bits mean unique document presence, and
duplicate matches cannot inflate counts.

`Docs::List` uses sorted document IDs and `Docs::Bits` uses exact 64-document
words. `apply` implements the specification's *left-associative* AND, OR and
AND-NOT operations; NOT subtracts the right operand rather than complementing
the entire universe. Phrase matching tests the ordered token sequence using a
rare anchor and reports each document at most once. Short-word hashing and
CRC32C hash tags are only prefilters; SIMD compares every candidate token's
complete 32-byte padded key before declaring it equal. No probability can
change correctness. Exact ID sum, total count and first ten document IDs are
calculated from the final set.

## Build and measured alternatives

`Dockerfile` pins its toolchain and runtime images by SHA256. The multi-stage
build uses generator-produced training input, Rust PGO+LTO and an offline
runtime with no additional runtime dependencies. The official reference
and independently generated tiny/sample seeds are compared byte for byte.

The highest recorded official result for this implementation is public-board
run `run-f979478c0277` (commit
`c9f733bb7849269b600358483cc1c06936318bb6`): **72.172141 points**,
**183.883 ms**, **46,641,152 bytes peak RSS**, **S=717**, **D=1**,
**K=27.761494**. All official build, correctness, timeout and sandbox gates
passed. This is a public-board observation; the final holdout may differ.

An alternative exact threshold (bitmap cutoff 1/64) and a four-token lookup
lag looked faster on a GitHub-hosted benchmark, but official run
`run-fb448cadcb4e` measured **71.521322 points**, **182.235 ms** and
**55,414,784 bytes**. Its roughly 19% higher scored memory outweighed the
slightly lower wall time, reducing K to 26.941790. It was rolled back by
`e2b1126f3066160af8ca82461cdd0aa06e1b5de6`. This demonstrates
why runtime-only promotion is unsafe. Full experiment history and additional
negative results are documented in `PERF.md`.

Future research: exact adaptive two-pointer/galloping intersection for
comparably sized postings, query-conditioned shared phrase execution and
better worker sharding; all must pass full input output hashes, independent
seeds, measured memory, then official board score before promotion.

## 2026-11-02: candidate windowed hybrid architecture (experiment branch)

This experimental version ports the MIT-licensed query-trigger and windowed set
execution design from [danse-macabre Track B](https://github.com/keirsalterego/danse-macabre/tree/speed-demon/track-b), authored by its contributors. Copyright (c) 2026 Hackathon Raptors CIC and danse-macabre contributors; MIT license and attribution retained. The upstream Track B source was accompanied by public score 79.5 / 87.8 ms / 42 MB; **these are upstream results, not results for this repo**.

Features: 1/64 sample to plan rare triggers; per-document truth-table boolean evaluation; query-window dense bitmaps + sparse document lists; AVX2 tokenizer; inline exact phrase verification. The original NewtonRedHeat best Track B source remains preserved in Git history at f9c0a75900b951ba5fcf8a4e4bd74a4b7a6266e6. Must pass full correct-output and official machine-score gates before production promotion.
