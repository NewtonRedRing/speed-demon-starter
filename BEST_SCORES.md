# Speed Demon 2026 — verified best-source consolidation

Team: `NewtonRedHeat`. Evidence: [official public-seed-1 board feed](https://s3.amazonaws.com/raptors.dev/speeddemon.dev/2026/board/board.json), generated **2026-11-02 10:37:46 UTC**. These are the latest *retrievable* historical best per-track runs at that feed timestamp; individual runs are not a benchmark of the merged branch head.

| Track | Official board best commit | Points | Runtime | Peak memory | Output gates |
|---|---|---:|---:|---:|---|
| A — Ingest | `eea0f63220e5c1a782c5627941d55f154833f8b9` | 83.585525 | 23.187 ms | 15,110,144 bytes | passed |
| B — Index | `126b40894cb67d44283296176cfaf17f300b2220` | 72.043345 | 185.745 ms | 46,850,048 bytes | passed |
| C — Join | `63ec07b1d10a6cd7fd9a169f9b9a80d684a03dd8` | 73.956052 | 37.853 ms | 10,215,424 bytes | passed |
| D — Traverse | `a4807ddacffcaf958e8cce89428777ffdc5f6445` | 67.766726 | 145.480 ms | 77,139,968 bytes | passed |

Historical note: an earlier board snapshot recorded **D 68.316671 points** from `6759b53eda0ced88ef30a8d0f9b77654a0c8686b` at 174.011 ms. That old best is not shown as the D best in the feed above; do not conflate the two snapshots or claim a new improvement.

## Verified source selection on speed-demon

The latest audited `speed-demon` branch head `c9f733bb7849269b600358483cc1c06936318bb6` contains identical scored implementation files for A/B/C compared with their best public runs:

| Track | Current `main.rs` and `Dockerfile` | Source comparison |
|---|---|---|
| A | Matches official best `eea0f63` | track subtree identical |
| B | Matches official best `126b408` | track subtree identical |
| C | Matches official best `63ec07b` | identical `main.rs`, `Dockerfile`, and `Cargo.toml`; only experimental notes changed |
| D | Matches older verified source `6759b53` | identical `main.rs` and `Dockerfile`; recent D board best commit is not retrievable as a GitHub object to compare directly |

The unmerged draft PRs contain unscored or rejected experiments; **none has verified superior official score** as of this audit. Do not merge them as score improvements without independent correctness and board evidence.

This documentation-only consolidation deliberately changes **no track source, build flags, runtime dependencies, or contest inputs**. The official holdout seed will ultimately determine the final scores.
