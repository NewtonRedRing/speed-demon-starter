# 100-trial speed-first optimizer for Tracks A, B, C and D

**Scope:** `experiment/100-trial-score-search-20261031`. This branch is based
on the current `speed-demon` and does not merge unverified old PRs.

## Current execution status

- **400 configurations defined:** 100 configurations for each of A/B/C/D.
- **0 official full-data configurations executed in this environment.**
  The ChatGPT container has no Docker engine or official A/B/C/D input set,
  and cannot access the public S3 dataset hosts from the container.
- GitHub Actions run
  [41153066376](https://github.com/NewtonRedRing/speed-demon-starter/actions/runs/41153066376)
  started but all four matrix jobs failed before recording any runner steps
  or usable logs. This is **not** validation or a score result.
- No Dockerfile has been promoted to a winning measured configuration;
  the default build parameters remain equivalent to `speed-demon`.

## The exactly 100 distinct candidates per track

Each track searches the Cartesian product of:

- worker threads: **6, 8, 10, 12** (4 settings)
- one track-specific parameter (5 settings)
- compiler strategy: baseline `-O3 -flto`, plus one of
  `-funroll-loops`, `-fno-tree-vectorize`, `-fno-ipa-cp`,
  or a final `-O2` override (5 settings).

| Track | Parameter | Five values |
|---|---|---|
| A | Per-hour record reserve divisor (bytes / parameter) | 256, 384, 512, 768, 1024 |
| B | Boolean posting-merge task size | 8, 16, 32, 64, 128 |
| C | Exact name dictionary buckets per stripe | 1024, 2048, 4096, 8192, 16384 |
| D | Graph input buffer in KiB | 64, 128, 256, 512, 1024 |

**4 × 5 × 5 = 100** configurations for every track, **400 total**.
These are reproducible engineering settings, not 400 invented algorithms.

The current baseline values are **12 workers** and parameter
**512 / 32 / 4096 / 256** for A/B/C/D respectively. The selected
configuration is rebuilt under the competition's pinned GCC 15 Dockerfile.

## How to run

On an x86-64 machine with Docker, GCC/g++, Python 3.13 and access to official
datasets:

```bash
git fetch origin
git switch experiment/100-trial-score-search-20261031

# Inspect all 400 planned, distinct configurations without compiling.
python3 scripts/optimize_100.py --plan

# Download and verify every official public seed-1 input file.
python3 scripts/fetch_data.py --tracks a b c d

# Run all 400 configurations and rank via exact-output SHA and local time/RSS.
python3 scripts/optimize_100.py --tracks a b c d --engine docker

# Only if you want to update the experimental Dockerfile build settings
# after a conservative improvement and exact validation:
python3 scripts/optimize_100.py --tracks a b c d --engine docker --apply

# Rebuild and verify each selected image against the official hashes:
python3 scripts/public_full_benchmark.py --tracks a b c d
```

Each candidate must produce the **official byte-for-byte SHA-256** output on
the official full seed-1 input. Incorrect candidates are excluded. One
screening run is taken per valid candidate, then the top eight receive five
new timing runs; the baseline is rechecked before and after to reduce
temporal-host-load bias. The report is saved as `.tuning/report.json`
(and `.tuning/` is gitignored). A candidate is only applied when its
time/RSS proxy beats the **better** of the two baseline medians by more than
1%, and the five-run checksum validation succeeds.

The `optimize-100.yml` GitHub Actions workflow requests four parallel
track jobs, each with all 100 configurations. When *all four* succeed, a
final promotion job can commit selected Dockerfile args to this experimental
branch; it never merges into `speed-demon`.

## Limitations and final scoring gate

This optimizer measures **process wall time and peak process RSS** on the
execution machine. The official competition uses its own CPU and cgroup v2
`memory.peak`, including kernel/output cache charges. Source-line and
dependency axes also matter; the local ranking is therefore a **proxy**, not
an official machine-point result. The winning setting can change across CPU
architectures. Do not advertise 90+, leaderboard rank or a guaranteed
score from this script.

Before merging into `speed-demon`, run official Docker validation on every
supported seed and compare end-to-end K under the official harness. Each
track's output is required to remain exact, including zero-edge shortest
paths, Unicode tokenization, CSV escapes, full unsigned latencies and
duplicate records.

If GitHub Actions lacks working runners (as observed here), use the commands
above on a properly provisioned local machine; the workflow alone cannot
make progress.
