# Track C: performance log

Every board run this track has scored, with the commit it measured. Judges can
check each run ID against the live board. Earlier local experiment logs are in
git history.

| Commit | Board run | T | M | Points | Notes |
|---|---|---|---|---|---|
| e8f4a84 (original C++) | run-1daf986bbf39 | 3.376534 s | 393.50 MB | 13.136 | Original submission |
| dfdc930 | run-8bf4bf303f0c | 0.109505 s | 17.04 MB | 62.331786 | |
| 1ba1831 | run-2d891a7fe8f2 | 0.102775 s | 17,166,336 B | 62.989248 | Published result for this commit |
| 2ef4945 | run-c08453a4bb25 | 48.717 ms | 11,825,152 B | 70.745228 | Correct; S=677, D=1 |
| c1e304d | run-840bad07699d | 48.498 ms | 28,590,080 B | 66.960059 | 2^18-slot name table; rejected |
| 39502cdb | run-0b1d0e65fdba | not recorded | 17,747,968 B | 68.996326 | Zero-copy EOF mapping; reverted |
| 3529757 | run-6d316a3b4177 | not recorded | 11,599,872 B | 70.969633 | Previous best |
| 719f5e2 | run-3c912b013ad6 | 44.564 ms | 11,374,592 B | 72.430291 | Earlier best; S=572, D=1 |
| 126b408 | run-df1b1d44c1d4 | 38.107 ms | 11,075,584 B | 73.442105 | Previous submitted best; S=732, D=1 |
| 63ec07b | run-70f424aa22a8 | **37.853 ms** | **10,215,424 B** | **73.956052** | **Current best**; S=712, D=1 |

Other scored runs: `run-7a3979243031` scored 70.905419, and
`run-0917a954f355` scored 70.830007 with M=11,579,392 B. Their commits are not
recorded in this log.

## Current submitted version

Commit `63ec07b` contains the FALCON-C fused structural scanner. The official
public seed-1 board run `run-70f424aa22a8` scored **73.956052 points** with
37.853 ms, 10,215,424 B peak memory, 712 measured source lines and no runtime
dependencies (D=1). Build, output hash, timeout and sandbox gates all passed.
Against the previous scored commit `126b408` (73.442105), this gains
**0.513947 points**. The source reads only the specified /data paths and writes
only the specified /out file.

## Rejected board experiments

- `c1e304d`, 2^18-slot name table (`run-840bad07699d`): 66.960059 points.
  T was 48.498 ms, but M rose to 28.59 MB. Reverted.
- `39502cdb`, zero-copy EOF mapping (`run-0b1d0e65fdba`): 68.996326 points.
  M rose to 17.75 MB. Reverted to the copied, padded tail.

## November 1: field-specific parser candidate

Local Docker measurements are not board measurements. With Linux-volume input,
the official monotonic timer, cores 0-11, fresh containers and tmpfs output,
12 interleaved runs per image gave trimmed means of 56.307 ms for the protected
baseline and 50.048 ms for the candidate (11.1% lower). Memory was noisy:
trimmed means were 12,819,456 and 14,067,200 bytes respectively. The candidate
also removes the general CSV structural stream and the pending-record queue.

Correctness passed the official full seed-1 hash, official-reference comparisons
on tiny seeds 2, 7, 42 and 2026, sample seeds 17 and 103, full seed 23, and a
separate quote/boundary/collision test including a 65,541-byte name. Test data
and benchmark scripts are outside the submission. A board result is required
before calling this an improvement to the submitted best. That result is now
recorded above: 72.430291 points. Cache-line-fused membership and predicted
membership prefetch were tested afterward but did not beat this candidate,
so both were removed.

Six workers were slower and were not retained.
Eliding hot-path bounds checks also failed to justify its memory variation
and additional unsafe invariants; the checked version was retained.

A bounded producer/consumer mailbox prototype routed name IDs to country owners
so only one worker updated each bitmap row. It matched the official full seed-1
hash but was slower in 12 paired local runs: 32.043 / 45.700 ms
(protected / prototype). The extra routing and synchronization were not retained.

## Rejected local experiments

Each of these matched the official output hash but was removed because it did
not improve elapsed time enough to justify its memory or source-size cost:

- Six workers, and a 1,024-bucket dictionary.
- A 1,024-entry per-worker exact-name cache, and a 2,048-bucket dictionary.
- Buffered record parsing, cache-line-separated membership, and arena-backed name views.
- Stripe-owned country deduplication: exact, but slower and about 72 MB.


## November 1: parallel page release

Each worker now hands the whole pages of every chunk it has parsed back to the
kernel (`madvise(MADV_DONTNEED)` on the file mapping), so the unmapping of the
202 MB input is spread over the 12 workers instead of being done by the
exiting process alone. The board measured 42.15 and 42.35 ms (72.847 points)
against 44.2-45.0 ms for the same source without it. Longer read-ahead (2 KB and
4 KB, against 1.5 KB) scored lower, as did aligning the per-country counters and
zero-allocating the name tables.

## November 2: window parser, bucket probe (local measurements only)

Local numbers are paired, interleaved Docker runs on a noisy shared machine (cores 0-11, official
timer); they rank candidates but are not board numbers. All kept changes match the official full
seed-1 hash, the official tiny/sample references, and an adversarial generator (quotes, `"""x"`
names, CR/LF inside quotes, 17/24/32/33-byte names, shared 8- and 16-byte prefixes, a 65,541-byte
name, BOMs, missing regions, 1-40 region files).

- Kept: one 128-byte window per record (quote parity by carry-less prefix xor), about 3-5% faster.
- Kept: drop the 1 MB `meta` array by keeping a long name's length in its slot, 1 MB less memory.
- Kept: probe a 4-slot, 64-byte bucket with two vector compares, about 6% faster.
- Kept: regions.csv loaded by all threads in parallel, about 3% faster (the serial load was ~2 ms).
- Kept: unchecked window reads and a BZHI quote count; branch-free name masks.
- Rejected: strided chunk claim order (spreads first page faults over page-table pages), neutral;
  64 chunks per thread, 5 ms slower; 16 per thread, same; `MADV_POPULATE_READ` before each chunk, 6 ms
  slower on a read-only floor test.

## November 2: FALCON-C continuous structural scanner

Experiments were developed on `experiment/track-c-falcon-tile-20261102`
and compared against unchanged `126b408`. Instead of independent 128-byte
record windows, the scanner processes successive 64-byte AVX2 tiles, carrying
quote parity and total quote counts across tiles. It identifies only commas and
newlines outside quotes, decodes the required numeric fields directly, and
uses the existing exact-name dictionary, per-country bitsets, region lookup,
thread-local numeric aggregation, and parallel page release unchanged.

Every input byte is classified at most once by the structural SIMD scanner for
the assigned range. The quote count is requested at only the name and note
boundary; three unnecessary quote prefix-count operations per record were
removed in the second iteration. Names and notes are never materialized.
Source lines decreased from 732 to 712 by replacing the window parser and its
fallback with a compact streaming parser.

GitHub Actions comparison and exactness checks:
- First fused candidate `589cf01`: full seed-1 SHA256 matched;
  interleaved non-PGO CI medians 215.126 ms baseline / 216.053 ms candidate
  (neutral; run 41419843807).
- Remove redundant quote counts `35cc42b`: full seed-1 SHA256 matched;
  216.924 / 216.960 ms (neutral; run 41420151351).
- Repeat with seed diversity (run 41420420506): 216.313 / 215.902 ms
  (candidate about 0.2% faster on this different, noisy GitHub CPU).
  The official full seed-1 hash matched. Generated official tiny seeds
  2, 7, 42 and 2026 all matched the official Python reference byte-for-byte.
  These CI timings are not scoring-box timings.

Official board experiment `63ec07b`, run `run-70f424aa22a8`, scored
73.956052 (37.853 ms, 10,215,424 B, S=712, D=1), improving on the
previous best `run-df1b1d44c1d4` at 73.442105 (38.107 ms, 11,075,584 B,
S=732, D=1). This is a verified public-dataset improvement, not a claim about
the unobserved holdout set.
