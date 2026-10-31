# Track B adaptive per-shard bitmap experiment — October 31, 2026

**Status: independently validated synthetic kernel, not an official submission.**
The production `speed-demon` branch is intentionally unchanged until the
full public SHA-256 and an end-to-end score are verified.

## Source-informed motivation

The current Track B C++ source records a flat 8-byte
`Local::Hit{boolean_term,document}` for each distinct membership, then
allocates a second 4-byte `Local::documents` posting array, and finally
constructs per-term bitmaps or sparse postings. Existing official-input
instrumentation reports ~21.1 million memberships and 53,444 Boolean terms;
this implies ~168.8 MB of hit payload and ~84.4 MB of temporary posting
payload (not necessarily all simultaneously resident at their maxima).
The board-confirmed known-good version has ~300 MB peak.

## New exact, online alternative: thresholded bitmap promotion

For each worker's disjoint, document-aligned shard, maintain:
- `last[boolean_term]`: last document ID, to deduplicate repeated words;
- `seen[boolean_term]`: distinct document count in this shard;
- `hot[boolean_term]`: optional, shard-local bitmap, initially absent;
- one existing flat `hits` vector for the cold prefix and cold terms.

On a newly observed (term,doc), append to `hits` while
`seen[term] <= threshold`. At threshold+1, allocate a bitmap sized to
the **worker's document ID interval**, and store subsequent document IDs
there. The first threshold hits remain in the flat list. This is an exact
partition of memberships, with no reprocessing, no query-frequency
assumptions and no dropped pilot segment.

For production integration, compute worker first/last document IDs at
the existing newline shard boundaries; avoid allocating a full-N bitmap
for every worker. Final posting assembly merges the old flat hits and
bitmap suffix into the existing exact dense/sparse Posting representation.
The query evaluator and Aho-Corasick phrase path are unchanged.

**Crucial:** the bitmaps must be correctly shifted/ORed if worker starts
are not aligned to 64-document boundaries. A cheap and safe implementation
enumerates set bits into the final posting; a faster implementation can
copy aligned interior words and OR the two boundary words. Do not
assume contiguous byte chunks imply aligned document IDs.

## New measured independent kernel

A local C++20 synthetic benchmark generated exactly 250,000 documents,
21,000,000 distinct Boolean memberships, 53,444 Boolean terms, 12
document-ID shards, and deliberate repeated tokens in every document.
The generator directly accumulated an independent per-term
(count, 64-bit sum, first-ten IDs) oracle. Both flat and adaptive
collectors reconstructed all 53,444 summaries from their stored
representations; all matched, including repeated-token deduplication.
Strict `-Wall -Wextra -Werror -pedantic` build passed; the adaptive
variant also passed ASan+UBSan+leak detection.

Representative resident-memory measurements, in **this isolated
synthetic kernel only**:

| Collector | Promotion threshold | Flat hits | Bitmap payload | Peak RSS |
|---|---:|---:|---:|---:|
| Flat baseline | none | 21.00 M (168.00 MB) | 0 | 184,972 KiB |
| Adaptive | 64 | 6.09 M (48.70 MB) | 146.21 MB | 210,792 KiB |
| Adaptive | **128** | **8.38 M (67.07 MB)** | **18.87 MB** | **108,644 KiB** |
| Adaptive | 256 | 9.27 M (74.16 MB) | 18.03 MB | 114,788 KiB |
| Adaptive | 512 | 11.04 M (88.31 MB) | 18.03 MB | 128,568 KiB |

Threshold 128 reduced the kernel's measured peak RSS by ~41.3%
relative to flat storage. A low threshold (64) was **worse** because
many medium-frequency terms were promoted and allocated expensive
bitmaps. This sharp crossover is a useful finding: threshold must
account for the per-shard bitmap byte cost and allocator overhead.
A practical per-shard break-even threshold is approximately
`ceil(shard_docs / 64) * 8 / sizeof(Hit)` before metadata and allocation
costs; add hysteresis above this bound.

Wall times varied with local host load; this is **not** an official
runtime improvement. The benchmark does not model the entire tokenizer,
automaton, query evaluation, final posting construction, Docker or cgroup
measurement. The real term distribution could differ substantially.

## Go/no-go to promote into speed-demon

1. Integrate in a separate candidate branch; use shard-ID boundaries
   determined from the actual input, not guessed fixed sizes.
2. Check all 14 existing regression tests plus Unicode, duplicate-token,
   partial bitmap word, threshold 127/128/129, empty term and mixed
   Boolean left-associativity oracles under ASan/UBSan.
3. Check the complete official seed-1 output SHA-256.
4. Compare alternating full-process runs and peak cgroup memory against
   known-good `425051dc91a0713b1c4e97e57308221a3df71dff`.
5. Push to `speed-demon` only if the combined exactness and scoring
   comparison is non-regressing; keep `425051d` as rollback.

This is an additional measured validation of the earlier
`research/TRACK_B_HOT_TERM_BITMAPS_2026-10-31.md` proposal, not a
claim that its score target has been achieved.
