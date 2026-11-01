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

# Build four submission Dockerfiles OFFLINE; run all official inputs.
# Each track runs 5 times and MUST match its official output SHA-256 every time.
python3 check.py all
```

For rapid iteration on just Track A, still using its complete original input:

```bash
python3 check.py a                    # 5 complete official full-data runs
python3 check.py a --runs 1           # quick iteration, still official data
python3 check.py a --skip-build       # repeat without rebuilding
```

Similarly, `python3 check.py b`, `c`, or `d`. The checker downloads a
missing official input automatically, verifies input SHA-256, runs the official
submission Dockerfile in a no-network, read-only-root sandbox, requires
exactly one output file, and verifies the **published official output SHA-256**
on every run (five runs by default, discarding fastest and slowest local times
for a non-authoritative trimmed mean).
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

## Optional Track A algorithm / PGO validation

The Track A code has a root-level Rust regression test (outside the scored
track directory). Run `rustc --edition=2021 -C target-cpu=native --test
tests/track_a_algorithms.rs -o /tmp/track-a-tests && /tmp/track-a-tests`.

To benchmark a separately trained PGO candidate **without changing the
official submission Dockerfile**, run:

```bash
docker build --network=none -t speeddemon-pgo/track-a \
  -f scripts/track-a-pgo.Dockerfile track-a
python3 check.py a --skip-build --image speeddemon-pgo/track-a
```

The two seed-2/3 generated files are exclusively **build-time training
inputs**; `check.py` always verifies the real full public seed-1 input and
the organizer's exact expected output SHA-256. Neither the optional Dockerfile
nor the algorithm test is inside `track-a/`, so they do not inflate the
submission's source-size count.

### Existing Windows / PowerShell checkout

Some previous checkouts keep the downloaded public files at
`data\\a\\full-seed1\\requests.log` with sibling
`tiny-ref` and `tiny-seed1` folders. The downloader recognizes this
layout automatically and leaves all tiny fixtures intact. **Only the
full-seed1 input is used after an exact SHA-256 comparison**.

From a Windows PowerShell prompt, build and verify Track A with:

```powershell
git pull --ff-only origin speed-demon
python3 check.py a --runs 1

# If your Linux Docker Desktop VM exposes fewer than 12 usable CPUs:
$env:SPEED_DEMON_CPUSET_CPUS = '0-3'
python3 check.py a --runs 1
```

Run the Rust-only algorithm tests **inside Linux**, not with the native
`stable-x86_64-pc-windows-gnu` compiler. PowerShell uses a backtick
for line continuation (not Bash's backslash):

```powershell
docker run --rm `
  --mount "type=bind,source=$($PWD.Path),target=/work" `
  -w /work `
  rust:1-slim-trixie `
  sh -lc 'rustc --edition=2021 -C target-cpu=native --test tests/track_a_algorithms.rs -o /tmp/track-a-tests && /tmp/track-a-tests'
```

The local Rust test container is not an official scored submission.


## 2026-11-01: measured Track A optimization workflow

The scored `speed-demon` branch restores **exactly** the source of the previous
best Track A result, `731690155806efd00de78190a438007beecbdd3f`:
79.686448 points, 30.690 ms, 21,938,176 bytes memory, and 348 formatted
source-code lines. The newer full fused/radix implementation is preserved in
`experiment/track-a-fused-radix` rather than silently replacing the best.
The active small-change candidate is `experiment/track-a-small-groups`.

Two independent, reversible experiment commits:
- `5bf7f3b9`: exact n=1/n=2 percentile fast paths.
- `89dae5fb`: reuse the per-worker hour-scatter cursor.

To compare them from **Windows PowerShell** or Linux/WSL with Docker Desktop
enabled, without editing your working tree:

```powershell
git fetch origin
python3 scripts/compare_track_a.py --runs 5
# Isolate the first change:
python3 scripts/compare_track_a.py --candidate-ref 5bf7f3b971bb6daf4a11da3309cda3816074ed49 --runs 5
# Isolate the second change (first change serves as control):
python3 scripts/compare_track_a.py --base-ref 5bf7f3b971bb6daf4a11da3309cda3816074ed49 --candidate-ref 89dae5fbf9f6f893bb13e1d399ed105e7e734f26 --runs 5
```

Every tested image compiles directly from the specified Git revision and
its digest-pinned `track-a/Dockerfile`. The local timer image is separate
from the scored submission and measures the child process only, excluding
Docker startup. Before accepting a timing, `check.py` enforces the original
238,836,234-byte seed-1/full dataset hash and matches every run's output to
the published SHA-256. The helper also prints cgroup-v2 `memory.peak` when
the local Docker VM exposes it.

**Local process timing is not the official score:** the organizer uses its
own i5-13500 CPU, higher-precision runner, cgroup peak, plus rustfmt/scc
static axes. Promote a candidate into `speed-demon` only if correct and
faster after accounting for memory and formatted source-size penalties.


### Experiment #1: prefault the exact original Track A input

`experiment/track-a-prefault` adds exactly one executable change to the
known-best Track A: `madvise(input_map, size, MADV_POPULATE_READ)` just after
the file-backed `mmap`. The call is timed, and the experiment fails if the
kernel rejects it. **Do not infer a speedup from fewer fault counters**:
`madvise` itself may incur the faults before the parser.

From **PowerShell**:

```powershell
git switch speed-demon
git pull --ff-only origin speed-demon
git fetch origin
python3 scripts/compare_track_a.py --base-ref origin/speed-demon --candidate-ref origin/experiment/track-a-prefault --interleave --runs 9
python3 scripts/compare_track_a.py --base-ref origin/speed-demon --candidate-ref origin/experiment/track-a-prefault --interleave --reverse --runs 9
```

The instrumented local timer prints per-run process time, child
`getrusage(RUSAGE_CHILDREN)` minor faults, and cgroup `memory.peak` when
available. Every run verifies the full public seed-1 input SHA-256 and
official output SHA-256. The timer and comparator live outside `track-a/`
and are **never part of the scored image**. Measurements are from your Docker
Linux VM, not the organizer's calibrated scoring CPU.

Keep `speed-demon` unchanged unless the candidate improves the complete
score tradeoff reliably and passes the official correctness gate.
