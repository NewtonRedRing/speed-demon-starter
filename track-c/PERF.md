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
| 719f5e2 | run-3c912b013ad6 | 44.564 ms | 11,374,592 B | **72.430291** | **Submitted best**; S=572, D=1 |

Other scored runs: `run-7a3979243031` scored 70.905419, and
`run-0917a954f355` scored 70.830007 with M=11,579,392 B. Their commits are not
recorded in this log.

## Submitted version

The submitted version is commit `719f5e2`, scored at 72.430291 points, up
1.460658 points from `3529757`. All board gates passed. Its
`main.rs` reads fixed input/output paths without environment overrides. The
offline build, sandbox and official full seed-1 output hash pass locally and
on the board.

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
