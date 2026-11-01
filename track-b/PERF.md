# Track B: performance log

Every board run this track has scored, with the commit it measured. Judges can
check each run ID against the live board. Earlier local experiment logs are in
git history.

| Commit | Board run | T | M | Points | Notes |
|---|---|---|---|---|---|
| (reference) | | 56.35 s | 2,200 MB | 0 | Reference, unchanged |
| e8f4a84 (original C++) | run-04e59f26d537 | 6.275489 s | 404.82 MB | 27.840 | Original submission |
| dfdc930 | run-2c406b83bfaf | 0.806407 s | 205.30 MB | 50.651659 | 533 formatted source lines |
| 2ef4945 | run-d270ce788113 | 238.498 ms | 51,601,408 B | 69.244251 | S=647, D=1, K=24.259677 |
| 6759b53 | run-712fb991a4c8 | 231.992 ms | 51,568,640 B | 69.739111 | Previous best; 610 lines |
| 0e3b9ac | run-56343bb059d4 | 209.118 ms | 51,953,664 B | 70.780882 | Role-specific scan; S=620, D=1 |
| 54de273 | run-830e8acc9e88 | 207.777 ms | 50,855,936 B | 70.917314 | Compact dictionary; S=625, D=1 |
| f0c2833 | run-8fc6c96282b9 | 206.138 ms | 50,802,688 B | 70.992252 | CRC32C hash; S=628, D=1 |
| ef8b612 | run-f988a68010bc | 203.795 ms | 50,561,024 B | **71.137073** | **Submitted version**; CRC32C, LAG=8 |
| b6f1ad7 | run-b6b1c11b6690 | 203.732 ms | 50,704,384 B | 71.128134 | LAG=4; below best |

## Submitted version

The best Track B board result is 71.137073 points on run `run-f988a68010bc`,
commit `ef8b612`, using the CRC32C hash and eight-token prefetch lag. A later
four-token-lag submission scored 71.128134, just below that protected best;
the source is restored to LAG=8. Both runs passed every board gate. The
requested 106 ms / 41 MB target is still unmet.

Run `run-d270ce788113` at `2ef4945` scored 69.244251 points. It was faster
and used less memory than the commit before it, but it has 37 more source lines
and scored lower overall, so it is not the submitted version.

## November 1: role-specific scan candidate

Bitmap terms now only OR membership bits; list terms alone deduplicate and
append postings. Unused terms and phrase-only anchors create no posting state.
The obsolete masked-write path was removed.

Local interleaved Linux-volume runs with the official timer and fresh sandboxed
containers consistently favored this approach, although the margin varied.
One 12-run-per-image comparison gave trimmed means of 217.290 ms for the
protected baseline and 205.659 ms for the role-specific scan (5.4% lower).
The final simplified writer was rechecked with 12 runs per image: 245.374 ms
versus 230.712 ms (6.0% lower), with essentially unchanged trimmed memory
(53,217,792 versus 53,248,512 bytes). These are local measurements, not board
results.

The board confirmed the improvement: 209.118 ms, 51,953,664 bytes, S=620, D=1
and 70.780882 points, recorded above.

Correctness matches the official full seed-1 hash and the official reference on
tiny seeds 2, 7, 42 and 2026 and sample seeds 17 and 103.

Rejected local alternatives: a 512-entry exact token cache, a second dictionary
prefetch stage, a prefix-only candidate hash, a 32-token prefetch lag, smaller
posting buffers, and generator-trained PGO. None justified its time, memory or
formatted-source cost. They are not in the submission.

## November 1: compact dictionary

Each dictionary slot now packs a 14-bit hash tag and 18-bit term ID plus one
into 32 bits instead of 64. Collisions still require full exact token equality.
The existing 131,072-term limit fits with room for the empty-slot sentinel.
Canonical formatted source is S=625, five lines above the protected version.

Two globally shuffled local comparisons disagreed on time: 199.789 / 186.842 ms
and 196.805 / 201.421 ms (protected / candidate). To control temporal drift, a
third comparison grouped runs into 24 pairs with independently randomized order
inside each pair. The candidate's median paired time ratio was 0.96680 and
mean ratio 0.97168; a seed-42, 10,000-resample paired bootstrap gave a 95%
mean-ratio interval of [0.95183, 0.99116]. It was faster in 16 of 24 pairs.
Trimmed means were 268.690 / 258.771 ms and 53,147,136 / 52,154,368 bytes.
Absolute local times vary substantially with host conditions.

The official sandbox/full seed-1 hash and reference comparisons on tiny seeds
2, 7, 42, 2026, sample seeds 17, 103 and full seed 23 all passed.
The board confirmed 70.917314 points, up 0.136432 from the protected
70.780882, with every gate passed. The compact dictionary is retained.

A separate cache-line-fused dictionary was rejected: 180.546 / 187.846 ms
and 53,586,432 / 68,510,208 bytes, with matching official output hashes.

An exact, immutable direct cache for tokens of at most three bytes was also
rejected: 16 paired runs gave 214.720 / 225.789 ms versus the compact
dictionary alone. Both matched the official full seed-1 hash.

Independent start/end token masks also did not improve trimmed time
(192.705 / 192.781 ms). A lower-instruction SIMD range classifier passed an
exhaustive 256-byte predicate check and the full output hash, but 40 paired
runs gave a bootstrap mean-time ratio interval of [0.96695, 1.00207].
Neither was retained.

Rebuilding the dictionary without phrase-verification-only terms matched the
full hash but gave only 205.882 / 204.231 ms in 16 paired runs, without a
meaningful memory reduction. That marginal result did not justify its extra
source and was removed.

Doubling scan-output buckets from 32 to 64 also matched the full hash but was
slower in 16 paired runs (216.288 / 225.605 ms trimmed time) and used about
0.57 MB more memory. The 32-bucket layout remains.

## November 1: CRC32C hash

The dictionary hash now uses hardware CRC32C over all four 64-bit lanes of the
exact, lowercase, zero-padded token key. The 32-bit result supplies the
18-bit table index and 14-bit candidate tag; complete 32-byte SIMD equality
still resolves every tag match, so collisions cannot change the result.
The official sandbox/full seed-1 hash and reference comparisons on tiny seeds
2, 7, 42, 2026, sample seeds 17, 103 and full seed 23 passed. Canonical
formatted source is S=628, three lines above the compact-hash version.

In 24 locally paired runs, the candidate was faster in 17 pairs. Its median
time ratio was 0.96647, mean ratio 0.96933, and seed-42 10,000-resample paired
bootstrap 95% interval for the mean ratio was [0.95109, 0.98748]. Trimmed
means were 231.887 / 223.489 ms (protected / candidate); memory was
52,045,312 / 52,152,064 bytes. The board confirmed the improvement: 70.992252
points, up 0.074938 from the compact-hash version, with all gates passed.

The four-token lag passed the same reference suite and looked faster locally
over 40 paired runs, but scored slightly below the best on the board. Lag 2
and 1 also matched the full hash but were slower than lag 4 in a 12-run
comparison (trimmed 223.030 and 227.463 ms versus 220.925 ms).

A 4-million-bit exact Bloom prefilter for query-term membership also matched
the full hash but was slower in a 16-run comparison (213.307 / 248.996 ms,
protected / candidate) and used more memory. It was removed.

## November 1: query evaluation without an index (rejected)

The scan was changed to keep no postings: each BOOL term sets a bit in a mask
for the current block of 64 documents, and when the block ends every query
that one of its terms can trigger (the first term, the rarer side of each AND,
both sides of each OR) is evaluated on the masks and keeps only its count,
sum and first ten. Output matched the official hash on the first run and peak
memory fell from 52 MB to 34-36 MB, but time rose from 195-230 ms to
260-300 ms in local paired runs: 18.6M query-block evaluations at about 30-100
ns each (instruction bound with two threads per core), against the 3,918
64-document blocks per query that a bitmap operation needs 3,918 words for.
Packing each query and its results into one 32-byte record, running the
quarter of queries with common triggers in every block, and branch-free
operators did not close the gap (ablation: 198 ms for the scan without any
evaluation, +60 ms for the common queries, +31 ms for the rest). The
postings-and-bitmap design is retained.

Reading ahead in the document text (1 KB or 2 KB ahead, per 64-byte window)
scored lower on the board in two runs (204 ms against 193-197 ms with the
unchanged source), as did a bitmap threshold of 1/16 (slower locally) and
1/64 (+8 MB).