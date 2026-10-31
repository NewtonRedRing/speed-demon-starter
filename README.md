# Speed Demon 2026 — NewtonRedHeat

Contest submission for all four Speed Demon tracks, on the `speed-demon`
branch. Every track is implemented in Rust with **zero runtime dependencies**.
Each `track-a/` to `track-d/` is an independent, offline, digest-pinned
Docker submission. Dataset files are **never embedded** in submission images.

## Local verification: always use the ORIGINAL official dataset

The local runner has been configured to use **only the organizer-published
full public seed-1 datasets** (not `tiny`, `sample`, or generated `full`
files). These are the exact 790.5 MB of public input used by the live board,
released at kickoff. Files and their expected outputs are pinned to the
organizer's `SHA256SUMS.json` and verified with SHA-256 before/after every
test. The **final holdout seed is unpublished**; nobody can download or
reproduce that final dataset before official scoring.

Official sources:
- https://speeddemon.dev/board/#datasets
- https://s3.amazonaws.com/raptors.dev/speeddemon.dev/2026/data/SHA256SUMS.json
- https://s3.amazonaws.com/raptors.dev/speeddemon.dev/2026/data/SHA256SUMS.txt

From the repository root, with Python 3 and Docker Desktop/Engine running:

```bash
git checkout speed-demon
git pull --ff-only origin speed-demon

# Download the seven ORIGINAL full-size published files for A–D (~790.5 MB).
python3 official_data.py download

# Independently verify every downloaded byte against the organizer's hashes.
python3 official_data.py verify

# Build four submission Dockerfiles OFFLINE; run all official inputs and
# require an exact match with the four official expected output SHA-256s.
python3 check.py all
```

For rapid iteration on just Track A, still using its complete original input:

```bash
python3 check.py a
python3 check.py a --skip-build      # run the already-built image again
```

Similarly, `python3 check.py b`, `c`, or `d`. The checker downloads a
missing official input automatically, verifies input SHA-256, runs the official
submission Dockerfile in a no-network, read-only-root sandbox, requires
exactly one output file, and verifies the **published official output SHA-256**.
A `--data` directory is accepted **only when every file is byte-identical
to the original full public dataset**. Passing a synthetic or alternative
`--expected` digest fails rather than silently accepting the wrong result.

Local data is cached at `data/a`, `data/b`, `data/c`, `data/d`, and
is excluded from Git via `.gitignore`. Do not copy data into a `track-*/`
folder or bake input/output data into Docker images; this is forbidden by the
competition. The track `pgo_gen.py` scripts exist **only for permitted
build-time training** of the PGO-enabled Dockerfiles, never to replace the
official inputs in local correctness testing.

## Runtime environment

`check.py` keeps the official sandbox defaults: offline build, offline
runtime, read-only filesystem, read-only `/data`, read-write `/out`,
`/tmp` as 1 GB tmpfs, 8 GB memory with swap disabled, 512-process limit,
unprivileged UID/GID, and CPU/NUMA affinity of `0-11`/`0`.

On a development machine with fewer than twelve usable CPUs, you can change
**only the local affinity** to make testing possible:

```bash
SPEED_DEMON_CPUSET_CPUS=0-3 python3 check.py all
```

This does **not** change input/output hashes or the submitted image. Local
wall-clock numbers, cgroup peak memory and CPU scaling are NOT authoritative
competition scores: only the organizer's Falkenstein i5-13500 scoring
machine and its final hidden holdout can give an official result. The
published full-scale inputs and expected outputs, however, can be replicated
**byte-for-byte**.

## Repository layout

- `track-a/` — Ingest, `/out/summary.txt`
- `track-b/` — Index, `/out/results.txt`
- `track-c/` — Join, `/out/aggregates.txt`
- `track-d/` — Traverse, `/out/distances.txt`
- `official_data.py` — immutable public seed-1 full dataset SHA-256 manifest,
  safe downloader and verifier
- `check.py` — required official dataset and expected-output gate for Docker
- `data/` and `.out/` — local untracked data and output directories

Each submission directory includes its pinned `Dockerfile`,
`.speed-demon.toml`, `Cargo.toml`, `main.rs`, `README.md` and `PERF.md`.
The Dockerfiles use Rust edition 2021 and `target-cpu=native`; B, C and D
also train and use PGO inside the offline build, using generators permitted
by the official rules. Do not confuse local test data with build-time PGO
training data.

The final machine score is the geometric mean of the best three eligible
track totals; each track's correctness and sandbox gates must pass.
