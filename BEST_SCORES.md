# Speed Demon 2026 — verified best public scores

Team: `NewtonRedHeat`. These are the historical **best per-track** official board
runs as confirmed by the live board feed on **2026-11-02 11:05:52 UTC**,
independently recorded in
[the board audit CI run](https://github.com/NewtonRedRing/speed-demon-starter/actions/runs/41433888424).
The current submission source for each track was separately compared against
the corresponding source in GitHub. Runs on the merged branch can score
differently due to timing/memory variance. The final holdout determines final
results.

| Track | Best public-board commit | Points | Runtime | Peak memory | Official gates |
|---|---|---:|---:|---:|---|
| A — Ingest | `c0053b4a77ec28817b83af2e6ee7492b10ae8c22` | **83.717827** | **22.451 ms** | 15,622,144 bytes | passed |
| B — Index | `c9f733bb7849269b600358483cc1c06936318bb6` | **72.172141** | **183.883 ms** | 46,641,152 bytes | passed |
| C — Join | `63ec07b1d10a6cd7fd9a169f9b9a80d684a03dd8` | **73.956052** | **37.853 ms** | 10,215,424 bytes | passed |
| D — Traverse | `6759b53eda0ced88ef30a8d0f9b77654a0c8686b` | **68.316671** | **174.011 ms** | 76,247,040 bytes | passed |

## Verified source selection on speed-demon

At the 2026-11-02 11:05 UTC audit, `speed-demon` was at
`c0053b4a77ec28817b83af2e6ee7492b10ae8c22`. The exact
`main.rs` blob SHA-1s for that branch were checked against GitHub refs:

| Track | Submission main.rs blob | Identical to scored reference |
|---|---|---|
| A | `da36728dd32ec3c0449fc23966d6e5cb6604d6ea` | `c0053b4` — HARP exact-hour cache |
| B | `c7dc80d3413beb6d0303bc376fe95e816a2b7b1c` | `c9f733b` and `126b408` |
| C | `e6782b241cd8f07bca5b114fdb05019a483f91e7` | `63ec07b` — FALCON-C |
| D | `46bfcda6d77a5bfb6a72a4baf108de4d71c81a1c` | `6759b53` — historical verified best |

Track A's newly promoted HARP hour-prefix cache passed the full official
correctness and sandbox gates at board run `run-1c2bb20981c8`. Its previous
best was **83.683039**, 23.108 ms from `c9f733b`. The new run scored
**83.717827**, 22.451 ms, improving by **0.034788 points** (and reducing
runtime by 0.657 ms). The small score gain despite the runtime improvement
reflects the scoring formula's other axes, including memory and source size.

The B and D scores above are historical board bests; they need not be the
latest timed runs. All four tracks use Rust with the pinned offline Docker
toolchains and no runtime dependencies. Separate experimental branches and
draft PRs are **not** promoted unless correctness and the official board
demonstrate an improvement. In particular, newer C wide-tile, atomic-filter,
PEXT and CRC variants did not outperform the retained FALCON-C version.

No holdout results are claimed. Source and scoring context:
[official board](https://speeddemon.dev/board/) and
[public board feed](https://s3.amazonaws.com/raptors.dev/speeddemon.dev/2026/board/board.json).
