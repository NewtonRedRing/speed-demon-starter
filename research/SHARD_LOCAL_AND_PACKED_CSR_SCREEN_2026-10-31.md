# Exact architecture screen: shard-local Boolean evaluation and one-pass packed CSR
Research date: 2026-10-31. Both are **isolated synthetic kernels**, not official dataset results.

## Why these two alternatives matter

Track B's best verified machine score remains 53.122178 (537.941 ms,
300.159 MB) vs target 69.556. Track D's best remains 58.796938
(360.109 ms, 136.016 MB) vs target 72.925. Board feed generated
2026-10-31T06:43:41Z. Current scored branch head is
`425051dc91a0713b1c4e97e57308221a3df71dff`.

## B: evaluate all Boolean queries inside each document shard

Instead of merging 12 workers' postings into a global inverted index, each
worker evaluates all 31,000 Boolean queries over its own disjoint document
range, producing (count, sum_of_global_ids, first_ten_global_ids). Reduce
these partial summaries in increasing shard order, concatenating only the
first 10 IDs. Phrase summaries can use the same ordered merge.

**Exactness proof:** every Boolean query is a pointwise predicate on one
document's set of tokens. Partitioning the documents into disjoint ordered
shards partitions the matching set. Count and sum are additive; the first
10 IDs in the union are exactly the first 10 of the ordered concatenation
of each shard's first 10. NOT remains left-associative set subtraction
within the shard; it is not a complement over the whole universe.

Independent Python oracle: 1,200 random corpora, 163,200 Boolean query
results, 111,223 total documents across trials, 9,696 shard segments.
All exact (including duplicate terms, absent terms, empty docs, NOT,
left-associative mixed operators and short last shards).

C++20 synthetic benchmark: 240,000 documents, 6,000 terms, 12,000
four-term queries, 12 shards, 18 token draws/document, same output
checksum `80685215058070` for global and shard-local. C++ kernel
compiled with `-O3 -march=x86-64-v3`, and both modes passed
AddressSanitizer/UBSan.

Three alternating runs with reusable AND scratch buffer:
- Global merge/evaluate: 201.101, 209.561, 190.124 ms total;
  peak RSS 48,128–48,256 KiB.
- Shard-local evaluate/reduce: 260.449, 259.470, 223.196 ms total;
  peak RSS 39,680 KiB.

**Result:** roughly 17.6% lower peak RSS but ~29% slower median total
time. Under the competition's T^0.50 / M^0.20 weighting, this is not a
clear gain and may be a regression. Do not merge into `speed-demon`.
The synthetic kernel does not include tokenization, phrases, or the
official query distribution. A query-planned shard-local implementation
could still be faster, but must demonstrate a complete run win.

## D: parse graph once into 8-byte packed arc buffer, then scatter CSR

For graphs with n <= 2^21, each directed arc is encoded as
`(uint64_t(from)<<37) | (uint64_t(to)<<16) | min(weight,65535)`.
The 16-bit sentinel is accompanied by an exact wide-weight side table
indexed by arc position. The fallback for larger n must be a generic
12-byte triple. After one parallel parsing pass, count degrees, prefix
sums and scatter packed arcs into forward and reverse CSR. This avoids
the second full text-parse pass in the current D implementation.

Independent exact edge-multiset oracle on 200 vertices and 5,000
random edges: both implementations produced the original arc multiset,
including parallel arcs, zero weights, 65,535 and UINT32_MAX.
Both modes produced identical full-synthetic adjacency checksums for
5 million arcs and 1,740,512 vertices:
`10968175775509498055`, including 1,079 wide arcs.

**Important benchmark correction:** an early version accidentally
used `auto cursor = g.offset` where `offset` was a C array,
which decayed to a pointer and mutated the prefix offsets.
The independent edge-multiset oracle caught this. All measurements
below are from the corrected `std::array` cursor copy.

Five/six alternating full-size synthetic runs:
- Two-pass text parser + CSR: 329.535, 300.529, 282.223, 282.030,
  322.043 ms; peak RSS ~180,052–180,240 KiB.
- One-pass packed-arc buffer + CSR: 250.389, 289.303, 205.878,
  271.320, 255.205 ms; peak RSS ~219,016–219,260 KiB.

**Result:** median graph-build improvement about 1.11x (300.5 to
255.2 ms) but ~21.6% higher peak RSS. Whole-process D still includes
2000 searches, so this is unlikely to close the 14.128-point gap alone.
On the target machine, the extra 40MB of memory traffic may further
change the result. No full official SHA validation; no submission change.

## Decision

Reject promoting either design from isolated results. Preserve the
scored baseline. Next higher-value candidates for B: inline-eight
postings PR #17 and adaptive phrase triggers, with full official hash
and end-to-end RSS/time. For D, profile parser vs query time before
paying for another global arc buffer. Investigate narrower 6-worker
search configurations and query-aware low-degree elimination only if
preprocessing is included in the timing.

Source of benchmark harnesses is retained in the automation working
directory, not part of the submission. No official performance claims.
