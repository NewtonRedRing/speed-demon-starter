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

## November 2: follow-on dictionary and hash experiments

Independent candidate branches were used; they do **not** change the
verified FALCON-C source retained on `speed-demon`.

- Dictionary prefetch batching: 8, 16, 32, 64, 128 sightings; official
  full-seed-1 output hash matched for all configurations. GitHub Actions
  medians were within 0.2% (`41428946618`). No clear winner.
- Reversible 6-bit inline packing of 17–20 byte name keys (with exact raw
  long-name fallback): full seed-1 and official tiny seeds 2, 7, 42, 2026
  matched reference. Paired GitHub Actions medians 215.918 ms baseline
  vs 216.337 ms candidate (`41429262686`): rejected.
- Record software-prefetch frequency (every record, disabled, every 2,
  every 4, every 8): official full seed-1 hash passed, medians were
  statistically indistinguishable (~165.4 ms; `41429522851`).
- Exact name hash mixer (5 variants): full seed-1 hash passed for all.
  The one-CRC candidate locally measured 165.221 ms versus original
  165.646 ms on one noisy paired batch (`41429780874`), but a second
  paired validation reversed the direction: 217.537 ms candidate versus
  216.450 ms baseline. All four additional tiny seeds passed
  (`41430010949`). An independent official board test was therefore
  required instead of trusting CI timing.

**Official board single-CRC test:** commit `843f284`,
run `run-869dd9a99af1`, passed all gates but scored **72.990104**
(40.873 ms, 10,543,104 B, S=711, D=1), below best FALCON-C
`63ec07b` run `run-70f424aa22a8` at **73.956052**
(37.853 ms, 10,215,424 B, S=712, D=1). The board also varies
between identical-source runs; these are observations, not a
causal estimate for the hash in isolation.

**Decision:** restore the known best source blob
`e6782b241cd8f07bca5b114fdb05019a483f91e7` via commit
`50fab2d`. Do not promote these inconclusive/rejected changes without
a new measured improvement.

## November 2: wide structural tiles and atomic bitmap filtering

All tests were isolated on experiment branches. The proven FALCON-C Rust
source (blob e6782b2) was left unchanged on the submission branch.

**Continuous tile-width sweep:** branch
experiment/track-c-wide-tiles-20261102, GitHub Actions
[run 41431626134](https://github.com/NewtonRedRing/speed-demon-starter/actions/runs/41431626134).
A fused comma-or-newline AVX2 mask and carry-less quote parity processed
1, 2, 4 or 8 consecutive 64-byte lanes per refill, comparing against the
existing FALCON-C scanner. Each variant matched the official full seed-1
SHA256, plus the official Python reference for tiny seeds 2, 7, 42, and
2026. Ten randomized interleaved non-PGO GitHub Actions timings:

| Variant | Median elapsed | Relative to original |
|---|---:|---:|
| original FALCON-C | 216.065 ms | 1.0000 |
| 64-byte fused | 216.358 ms | 1.0014 |
| 128-byte fused | 216.376 ms | 1.0014 |
| 256-byte fused | 216.790 ms | 1.0034 |
| 512-byte fused | 216.617 ms | 1.0026 |

The larger-tile versions were longer and did not earn a runtime speedup
on the CI CPU. They were not submitted to the scoring box.

**Exact distinct-name membership filtering:** branch
experiment/track-c-membership-filter-20261102, GitHub Actions
[run 41431876167](https://github.com/NewtonRedRing/speed-demon-starter/actions/runs/41431876167).
A Relaxed bitmap load checks whether a bit is already set; if not,
the existing atomic fetch_or performs the update, preserving safety
under concurrent races. Four official-full-hash-correct variants and
four official tiny seeds passed; twelve interleaved non-PGO timings:

| Variant | Median elapsed | Relative |
|---|---:|---:|
| original | 165.166 ms | 1.0000 |
| atomic load before fetch_or | 166.004 ms | 1.0051 |
| omit bitmap write-prefetch | 165.209 ms | 1.0003 |
| load filter + omit prefetch | 166.665 ms | 1.0091 |

All differences were small; the conditional update consistently added
work, and removing the write-prefetch was neutral. Rejected.

**Source-size rule:** the machine measures scc code lines after the
canonical language formatter, excluding comments and blank lines. Pure
minification is not a viable optimization, and the human code-quality
and optimization-log criteria also reward readable explanations. No
source-only cosmetic contest change was made.

Next research: CPU profiling of the name-table probe and
RMW cache-to-cache traffic, then benchmark a genuinely different
exact-name data layout, not marginal SIMD-width or prefetch tweaks.

## November 2: exact BMI2 PEXT 17-byte key experiment

Experiment branch: experiment/track-c-pext17-20261102; validation
[run 41432699497](https://github.com/NewtonRedRing/speed-demon-starter/actions/runs/41432699497).
A reversible 119-bit representation of exactly 17 raw ASCII name bytes uses
BMI2 PEXT on two 8-byte words, plus the final byte, with a separate tag
in the 128-bit name slot. Unlike a hash fingerprint, this is injective
for the supported 7-bit ASCII input. Other lengths retain the existing
byte-for-byte arena path. The aim was to avoid long-name arena traffic
on 17-byte keys. The official full seed-1 hash and tiny seeds
2, 7, 42 and 2026 all passed against the official Python reference.

Paired non-PGO GitHub Actions medians: baseline 215.247 ms,
PEXT17 216.295 ms (candidate 0.5% slower). Rejected, since the
candidate also adds measured source lines. No official leaderboard
submission was made for this change.

All November 2 follow-on performance experiments remain off the
speed-demon contest source. The best official board run remains
run-70f424aa22a8: 73.956052 points, 37.853 ms, 10,215,424 B.

## November 2: exact-name table placement and membership-ID sweeps

Three independent experiment branches compared seven new Rust variants to
the existing FALCON-C. The submitted Track C file remained untouched.
Every variant passed the SHA256 of the official full seed-1 output, plus
byte-for-byte comparison with the official Python reference on tiny seeds
2, 7, 42, and 2026. Timings below are non-PGO GitHub Actions runner
measurements, not official scoring-box results.

**Exact first-slot / first-two-slot / four-slot probes:**
[run 41444545241](https://github.com/NewtonRedRing/speed-demon-starter/actions/runs/41444545241).
12 randomized interleaved trials per binary:

| Variant | Median elapsed | Change |
|---|---:|---:|
| Existing 4-slot SIMD FALCON-C | 215.711 ms | baseline |
| First-slot scalar check before SIMD | 215.466 ms | -0.11% |
| First-two-slot scalar check before SIMD | 215.304 ms | -0.19% |
| Four-slot scalar lookup, no SIMD | 216.676 ms | +0.45% |

The small scalar-first improvements were below measurement noise and not
strong enough to justify changing code or increasing source length.

**Dense exact-name membership IDs:**
[run 41444955226](https://github.com/NewtonRedRing/speed-demon-starter/actions/runs/41444955226).
The candidate assigned atomic sequential IDs upon publication of exact
name slots, preserving collision handling and the full bitmap capacity.
This could reduce sparse membership bitmap working set but adds an extra
atomic ID lookup per record. 16 interleaved trials:

| Variant | Median elapsed | Change |
|---|---:|---:|
| Existing slot-indexed bitmap | 215.675 ms | baseline |
| Dense ID mapped via atomic side array | 215.902 ms | +0.11% |

**Direct home hashing vs four-way bucketized probing:**
[run 41445273213](https://github.com/NewtonRedRing/speed-demon-starter/actions/runs/41445273213).
Instead of aligning every hash home to four slots, two variants used a
precise one-slot home or a two-slot-aligned home and scalar lookups, then
the same exact linear fallback. 16 interleaved trials:

| Variant | Median elapsed | Change |
|---|---:|---:|
| Existing 4-slot SIMD bucket | 215.568 ms | baseline |
| Direct single-slot home | 215.772 ms | +0.09% |
| Two-slot-aligned home | 215.822 ms | +0.12% |

All seven were correct, but **none demonstrated a sufficiently reliable
speed and memory improvement** to justify an official board experiment.
Rejected without promoting implementation changes. The source blob
`e6782b241cd8f07bca5b114fdb05019a483f91e7` stays on
`speed-demon`; previous best public board run is `run-70f424aa22a8`,
73.956052 pts / 37.853 ms / 10,215,424 B. No hidden-holdout claim.


## November 2, 12:30–12:43 UTC — exact batch/chunk sweep and official rejection

**Hypothesis.** FALCON-C uses 16 pending name lookups per batch and 32 chunks
per worker. Smaller batches reduce live speculative name-cache traffic;
more chunks can lower peak scratch allocation. Neither change modifies the
exact UTF-8/quote-state parser, canonical hash/table comparisons, country
aggregation, or output order.

**Reproducible local experiments.** Initial nine-way research sweep in
[PR #52](https://github.com/NewtonRedRing/speed-demon-starter/pull/52),
[GitHub Actions 41445259502](https://github.com/NewtonRedRing/speed-demon-starter/actions/runs/41445259502),
passed tiny seeds 2/7/42, sample seed 103 and full seed 1 output byte-for-byte.
Its 24 GitHub-runner full-input timing trials suggested batch=8 at
98.143 ms versus baseline 99.151 ms, but the difference was small and
the runner is **not** the official scoring CPU.

Focused independent six-way confirmation in
[PR #54](https://github.com/NewtonRedRing/speed-demon-starter/pull/54),
[Actions 41445573587](https://github.com/NewtonRedRing/speed-demon-starter/actions/runs/41445573587)
passed three tiny seeds, sample 103 and full seed-1
SHA-256 `d8088b1cd42ba1338b1345b107b55db7b96ef1f5066f3ff64fd26a8912a0f536`.
Across 48 randomized interleaved full-run repetitions, median baseline
was **162.6095 ms** and combined batch=8/chunks=64 was **163.9556 ms**
(**0.83% slower**). Eight separate memory readings showed median RSS
**19,028 KB** versus **16,266 KB** (**14.5% lower**). This suggested a
possibly favorable complete K despite slower wall-time; differences are
specific to the shared GitHub runner and not equivalent to official memory.

**Official score-gate result.** A clean, scoped, two-constant
[PR #56](https://github.com/NewtonRedRing/speed-demon-starter/pull/56)
was temporarily integrated as `c3ce73af`. Public-board run
**`run-1d9635c77406`** passed correctness and all gates but recorded
**74.154112 points / 37.009 ms / 10,326,016 bytes / S=712 / D=1 /
K=30.414610**, below the historical exact FALCON-C winner
**`run-6dcd8ad2012f`**, **74.320728 points / 36.405 ms /
10,354,688 bytes / S=712 / D=1 / K=30.648876**. The slight memory
gain did not offset worse wall time on the real scoring machine.

**Decision: REJECT.** Oracle rollback commit
`ea2532f68462ca5fc07c3e93cf28d62d80a163c1` restored
`track-c/main.rs` byte-for-byte to the best-scored FALCON-C Git blob
`e6782b241cd8f07bca5b114fdb05019a483f91e7`.
The winning program, offline Rust toolchain and other tracks are unchanged.
This is **public-board evidence**, not hidden holdout evidence.
