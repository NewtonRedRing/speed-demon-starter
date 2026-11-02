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


## November 1: experiments that did not help

Answering BOOL queries without an index (per-block masks, queries triggered by their rarest terms) cut peak memory from 52 MB to 35 MB but raised time from about 200 ms to 260-300 ms (about 18.6M query-block evaluations at 30-100 ns each, instruction bound with two threads per core). Reading ahead in the document text (1-2 KB), bitmap thresholds of 1/16 and 1/64, handing the postings back to the kernel in parallel before exit, guided query scheduling and removing a per-query allocation were all neutral or worse on the board, so the postings-and-bitmap design stands.


## 2026-11-02 Track B cutoff and lookup-wavefront official score gate

**Hypothesis.** For this 250,000-document, 50,000-query workload, increase
the range of term frequencies represented with sparse postings by changing
the threshold from `df * 32 <= sampled` to `df * 64 <= sampled`.
Shorten the token lookup wavefront from 8 to 4 entries to reduce ring
work and improve critical-path instruction locality. Both are
representation/scheduling choices, not approximations: every query
still executes exact left-associative Boolean semantics, AND/OR/NOT,
phrase matching, and exact token membership checks.

**Exploratory differential CI:** [PR #51, run 41444995988]
(https://github.com/NewtonRedRing/speed-demon-starter/actions/runs/41444995988)
compiled ten Rust variants, compared tiny seeds 2/17/42/2026,
sample 103 and full seed 1 byte-for-byte, then tested twelve randomized
end-to-end full-dataset runs on a **GitHub-hosted CPU**, not the
organizer's machine. Lower threshold density64 showed a 3.63%
median timing benefit with more memory.

**Focused confirmation:** [PR #55, run 41445832177]
(https://github.com/NewtonRedRing/speed-demon-starter/actions/runs/41445832177)
passed tiny seeds 7/42, sample 103 and full seed 1 (public
`results.txt` SHA256
`f86973c210228141bcb6f1f1b10188ad2a9b65f71e7cd7cf4590b38de4e80927`).
Across 24 randomized interleaved full-corpus GH runs, old threshold
32 and LAG8 median **677.416 ms** versus threshold64/LAG4
**655.581 ms**, **3.22% lower** (21/24 paired wins).
Median of eight GH-run RSS readings: **299,994 KB** baseline,
**302,664 KB** candidate (+0.89%).
No new runtime dependency; source line count unchanged.
The non-scoring full-pipeline estimates appeared favorable.

**Exact official trial and regression:** A scoped two-constant
[PR #58](https://github.com/NewtonRedRing/speed-demon-starter/pull/58)
was merged temporarily as `4168def59b0873a305b53155b75547c35eaaa5c4`.
The organizer's public-board run
**`run-fb448cadcb4e`** (head
`c6ab79b8a40c183f9b270978c825accfe158d07c`, same Track B
source) passed **all** correctness, build, sandbox and timeout gates.
It reported **71.521322 points**, **182.235 ms**,
**55,414,784 bytes peak memory**, **717 source lines**,
**D=1, K=26.941790**. Historical best
`run-f979478c0277` with the original production source was
**72.172141 points**, **183.883 ms**,
**46,641,152 bytes**, **717 source lines**,
**D=1, K=27.761494**.

The candidate reduced wall time by 1.648 ms but increased the
**official** peak memory by about 18.8%, so the *total efficiency
coefficient regressed by ~2.95%*. This directly contradicts the
predictive GitHub-hosted RSS measurements. Hardware/cgroup measurement
differences are precisely why full official scoring is mandatory.

**Oracle decision: REJECT and restore exact winner.**
Commit `e2b1126f3066160af8ca82461cdd0aa06e1b5de6`
restores the best-scorer Rust source blob
`c7dc80d3413beb6d0303bc376fe95e816a2b7b1c`
byte-for-byte. Other tracks, offline build, source size and
dependencies were not changed. The scoring result above is public
seed-1 practice evidence, not a hidden holdout claim.

**Related Rust-vs-C reproducible control:** [research PR #35,
CI 41432940269](https://github.com/NewtonRedRing/speed-demon-starter/actions/runs/41432940269)
used the identical Rust engine but replaced only hot dictionary
lookup via C compiled with GCC or Clang. All output bytes matched.
Twelve full GH runs: native Rust **823.526 ms**,
Clang-C-FFI **831.926 ms**, GCC-C-FFI **850.851 ms**.
This is evidence against a C FFI lookup rewrite; it does **not**
prove an independently optimized all-C implementation cannot win.
