# Speed Demon 2026 — NewtonRedHeat

Contest submission for all four Speed Demon tracks, on the `speed-demon`
branch. Every track is implemented in Rust with **zero runtime dependencies**.
Each `track-a/` to `track-d/` is an independent, offline, digest-pinned
Docker submission. Dataset files are **never embedded** in submission images.

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

The final machine score is the geometric mean of the best three eligible
track totals; each track's correctness and sandbox gates must pass.



