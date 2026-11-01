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
| 3529757 | run-6d316a3b4177 | not recorded | 11,599,872 B | **70.969633** | Submitted best |

Other scored runs: `run-7a3979243031` scored 70.905419, and
`run-0917a954f355` scored 70.830007 with M=11,579,392 B. Their commits are not
recorded in this log.

## Submitted version

The submitted version is commit `3529757`, scored at 70.969633 points. Its
`main.rs` reads a fixed input path and writes a fixed output path, with no
environment overrides. A later cleanup removes the test-only environment hooks
for paths, which leaves the program's behavior unchanged. That cleanup passed
the local `check.py c` gate: the offline build and the sandbox run both
succeeded, and the output hash matches the official `expected_output_sha256`.
It has no board run yet.

## Rejected board experiments

- `c1e304d`, 2^18-slot name table (`run-840bad07699d`): 66.960059 points.
  T was 48.498 ms, but M rose to 28.59 MB. Reverted.
- `39502cdb`, zero-copy EOF mapping (`run-0b1d0e65fdba`): 68.996326 points.
  M rose to 17.75 MB. Reverted to the copied, padded tail.

## Rejected local experiments

Each of these matched the official output hash but was removed because it did
not improve elapsed time enough to justify its memory or source-size cost:

- Six workers, and a 1,024-bucket dictionary.
- A 1,024-entry per-worker exact-name cache, and a 2,048-bucket dictionary.
- Buffered record parsing, cache-line-separated membership, and arena-backed name views.
- Stripe-owned country deduplication: exact, but slower and about 72 MB.
