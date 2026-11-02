# Speed Demon 2026 — NewtonRedHeat

Contest submission for all four Speed Demon tracks, on the `speed-demon`
branch. Every track is implemented in Rust with **zero runtime dependencies**.
Each `track-a/` to `track-d/` is an independent, offline, digest-pinned
Docker submission. Dataset files are **never embedded** in submission images.

## Evidence-based submission selection

The **official public practice board** scores performance and efficiency; the
final championship additionally includes independent human review. The
organizer records `speed-demon` at the November 2, 2026 **15:00 UTC**
freeze and scores a previously unseen holdout. No holdout result is
available at submission time.

Historical *best* public practice runs recorded through the
[12:50 UTC Oracle audit](https://github.com/NewtonRedRing/speed-demon-starter/actions/runs/41447919837):

| Track | Public best | K | Wall time | Verified run |
|---|---:|---:|---:|---|
| A | **84.279208** | 48.482406 | 21.662 ms | `run-d45aa4ea3d05` |
| B | **72.172141** | 27.761494 | 183.883 ms | `run-f979478c0277` |
| C | **74.320728** | 30.648876 | 36.405 ms | `run-6dcd8ad2012f` |
| D | **68.363711** | 23.295618 | 168.724 ms | `run-f0a2ded6fcfd` |

Those historical results **are not all from the same commit**. The D high
watermark used an experimental two-level wheel queue, but another
official wheel run regressed significantly. The submission therefore
retains the more stable exact **radix heap**; do not infer that D's
historical best used the current radix source.

The selected Rust source is verified against previously scored,
all-gates-passing files:

| Track | Selected `main.rs` Git blob | Algorithm |
|---|---|---|
| A | `4559a57b037785452dcf64f4e098456440f96cd8` | Fused HARP parser, 8K exact interner, 12 workers |
| B | `c7dc80d3413beb6d0303bc376fe95e816a2b7b1c` | Exact query-conditioned inverted index, sparse posting lists + bitmaps |
| C | `e6782b241cd8f07bca5b114fdb05019a483f91e7` | Quote-correct SIMD FALCON-C join + aggregation |
| D | `46bfcda6d77a5bfb6a72a4baf108de4d71c81a1c` | CSR, BFS locality renumbering, exact radix-heap Dijkstra |

Every track's `PERF.md` distinguishes reproducible local experiments from
official practice-board scores, provides real run IDs, and explains
rejected alternatives. A/B/C retained implementations match their
historical best-scored source blobs; D retains the verified radix
implementation rather than its variable wheel alternative.
The externally conducted *build, correctness, sandbox and timeout*
gates passed for these recorded candidate sources. No submitted
container uses precomputed answers, hidden data, or runtime network
access. All images use digest-pinned build/runtime bases.

## Local verification

Inputs are the organizer's published full seed-1 datasets, kept in `data/`
(gitignored) and checked against `data/SHA256SUMS.json`. Run one track from
the repository root:

```bash
python3 check.py a --data data/a/full-seed1 --expected 495ca09682b9673ef836a506496177680bb4a3af54255174befddb1f83481cd3
```

The expected digests for tracks B–D are in `data/SHA256SUMS.json`. Use
`--skip-build` to rerun the last image. `check.py` builds offline and runs
the image in the board's sandbox (no network, read-only root, 8 GB memory,
512 processes, 1 GB `/tmp`, unprivileged user).

Local wall-clock numbers are for iteration only. The organizer's scoring
machine gives the official results. Do not copy input or output data into a
`track-*/` folder or bake it into an image. The track-c `pgo_gen.py` is a
byte-identical copy of the published generator, used only for build-time PGO
training.

## Repository layout

- `track-a/` — Ingest, `/out/summary.txt`
- `track-b/` — Index, `/out/results.txt`
- `track-c/` — Join, `/out/aggregates.txt`
- `track-d/` — Traverse, `/out/distances.txt`
- `check.py` — the organizer's gate: offline build, sandboxed run, output SHA-256 check
- `data/` and `.out/` — local untracked data and output directories

Each submission directory includes its pinned `Dockerfile`,
`.speed-demon.toml`, `main.rs`, `README.md` and `PERF.md`.
The Dockerfiles use Rust edition 2021 and `target-cpu=native`. Track C also
trains and uses PGO inside the offline build, using a generator permitted
by the official rules. Do not confuse local test data with build-time PGO
training data.

The final **championship score** is the geometric mean of the team's best
three track **totals**, combining 65% machine performance and 35% human
judging for each track. All tracks must independently pass the correctness
and sandbox gates. The public board displays machine performance, not
final championship points.



