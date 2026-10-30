# Speed Demon 2026 - NewtonRedHeat

Exact-output C++20 submissions for all four [Speed Demon](https://speeddemon.dev/)
tracks. No third-party runtime dependencies. Team metadata is `NewtonRedHeat`.

**Publication authorized by the team on October 30 after local validation.
Neither first place nor the requested 90+ points on every track has been reached
or verified.** Local performance is not an official leaderboard measurement.

## Required repository contents

- Each `track-a` through `track-d` contains its pinned offline `Dockerfile`,
  `.speed-demon.toml`, `src/main.cpp`, approach `README.md` and `PERF.md`.
- `check.py` checks the production-style sandbox and exact output hash.
- `scripts/fetch_data.py` downloads or verifies official public inputs.
- `scripts/benchmark.py` measures the solution process, excluding Docker startup.
- `tests/` contains independent solution oracles and dataset-integrity tests.
- `LICENSE` retains the MIT licence.

Obsolete phase reports, the stale board snapshot, duplicate templates, unused
practice sources and generated binaries were removed. All source files inside a
track count toward its source penalty, even when not compiled. The separate
practice repository was not changed. Downloaded data and generated outputs are
ignored by Git.

## Official public dataset

The [official board](https://speeddemon.dev/board/) publishes full public seed 1:
seven input files totaling 790,519,861 bytes, with an authoritative
[SHA-256 manifest](https://s3.amazonaws.com/raptors.dev/speeddemon.dev/2026/data/SHA256SUMS.json).

From the repository root on Windows, with Python 3.11+ and Docker:

```powershell
python scripts\fetch_data.py
```

Every existing file is hashed before reuse; missing or invalid files download
to temporary files and replace existing inputs only after successful SHA-256
verification. Network and checksum failures are explicit. `--force` requests a
fresh download even if the existing hash matches.

All seven local input hashes have been verified against the official manifest.
A redundant forced re-download was interrupted because the connection delivered
only about 27 KB/s; its partial files were removed. The verified complete inputs
remain intact. The saved manifest is `data\SHA256SUMS.json`.

Benchmarks use only the full datasets and hashes linked from
[the official dataset section](https://speeddemon.dev/board/#datasets).
Before each benchmark batch, the tool fetches the official manifest again and
verifies every selected input hash. Expected output hashes come directly from
that manifest, with no local-reference or hardcoded-hash fallback. Network or
checksum failure aborts the benchmark. JSON reports record the manifest URL and
input hashes.

Inputs live in `data\<track>\full-seed1`. Historical `tiny-seed1` and `tiny-ref`
fixtures are not used by the benchmark. Generated edge-case oracle tests are
supplemental software tests only, never official datasets or score measurements.
Final scoring uses a hidden holdout seed, not the public seed.

## Algorithms and measured local results

| Track | Approach | Previous local median | Candidate median | Candidate peak RSS |
|---|---|---:|---:|---:|
| [A](track-a/README.md) | Parallel streaming, exact endpoint interning, percentile selection | 0.541742 s | 0.064004 s | 40.90 MiB |
| [B](track-b/README.md) | Token phrase automaton, compact sparse/bitmap Boolean postings | 5.749076 s | 0.834935 s | 202.97 MiB |
| [C](track-c/README.md) | Quote-safe parallel CSV, exact shared names, atomic country membership | 3.299495 s | 0.118340 s | 14.94 MiB |
| [D](track-d/README.md) | Buffered packed bidirectional CSR, sparse radix-queue Dijkstra | 7.028240 s | 0.356099 s | 135.66 MiB |

Baseline medians use three repetitions; candidate medians use five. Every run
matches the published output hash. These are measurements from a shared local
Docker host, not the competition CPU. Host load materially changes timings;
the ratios are indicative, not a controlled hardware comparison or official
points. RSS comes from child-process resource accounting and has a
launcher-related floor on small inputs. A and D trade extra memory for speed.

Recent tuning bounds phrase results to count/sum/first-ten summaries instead of
retaining every matching ID, reduces B's posting-header allocation and glibc
arena fragmentation, avoids unnecessary C membership read-modify-writes, and
replaces per-line D stdio calls with buffered newline scanning.

Per-track [A](track-a/PERF.md), [B](track-b/PERF.md),
[C](track-c/PERF.md) and [D](track-d/PERF.md) logs distinguish actual historical
board runs from local candidates. No board run IDs or points were fabricated.

## Validation

```powershell
python scripts\benchmark.py a b c d --runs 1 --build
python -m unittest discover -s tests -v
python scripts\benchmark.py a b c d --runs 5
python check.py a --data data\a\full-seed1 --skip-build --expected 495ca09682b9673ef836a506496177680bb4a3af54255174befddb1f83481cd3
```

Repeat the last command for B/C/D using their manifest's expected output hash.
Windows checks transfer files through temporary Docker volumes; host-drive
sharing is unnecessary. Benchmark JSON can be saved using `--output <path>`.
Tests require the four previously built images.

Verified for this candidate:

- All four tiny outputs and five full repetitions per track match exact hashes.
- All four full-data images pass the capability-free production-style sandbox.
- Eight solution-oracle methods pass; six dataset-integrity tests check
  downloads, manifest validation and rejection of missing or modified inputs.
- Solution oracles also pass AddressSanitizer and UndefinedBehaviorSanitizer.
  Sanitizer images are separate from optimized images; ASan's own allocator does
  not use the glibc arena setting.
- All four sources pass GCC `-Wall -Wextra -Wpedantic -Werror`.

Oracles cover percentile ranks, chunk boundaries, empty inputs, ASCII/Unicode
token separators, overlapping/duplicate phrases, Boolean order, sparse/dense
merges, CSV quoting/BOMs/newlines/exact names, and random directed graphs with
zero and full-width weights, including parallel graph construction.
They do not prove every possible holdout case or an official performance score.

## 90-point target: quantified gap

The [official formula](https://github.com/hackathon-raptors/speed-demon-2026/blob/main/harness/scoring.py)
is:

```text
K = (T_ref/T)^0.50 * min(M_ref/M,50)^0.20 * min(S_ref/S,1)^0.15 * (D_ref/D)^0.15
points = clamp(50 * log10(K), 0, 100)
overall = geometric mean of the best three track points
```

`S` is canonical-formatted source size, not raw line count. `D` is one plus the
direct dependency count; ours is one. Source and dependencies can only penalize.
Final judging also contributes separately; the live board shows machine points.

90 points requires `K >= 63.095734`. Even with the maximum 50x memory bonus,
no source penalty and no dependency penalty, the required times are at most:

| Track | Optimistic time ceiling for 90 points |
|---|---:|
| A | 0.038493 s |
| B | 0.067686 s |
| C | 0.017349 s |
| D | 0.087313 s |

Actual memory and source penalties make the required times lower. The candidate
does not meet these optimistic ceilings locally. This is not a claim that the
target is impossible, but it rules out claiming success from the current data.

The fetched board snapshot generated `2026-10-30T18:58:52Z` had a leading overall
of 73.194 and no 90-point track. NewtonRedHeat's historical best remained 33.267,
rank 36 in that snapshot, from `e8f4a84`. Those are not results for this candidate.
Official improvement remains pending a scored board run after publication.

## Remaining optimization work

| Track | Remaining cost | Next evidence needed |
|---|---|---|
| A | Parsing/hash lookup, group allocations and merging | CPU profile and controlled allocation/percentile benchmarks |
| B | Corpus dictionary lookup and posting construction/merge memory | Cache-miss profile; compare compressed or two-pass exact postings against added scan cost |
| C | Quote-parity pass, exact-name lookup and shared cache traffic | Memory-bandwidth/atomic-contention profile on equivalent hardware |
| D | Random CSR degree/fill writes and search expansion | Construction/search profile; test preprocessing only including its full time/memory cost |

Earlier dense-distance D experiments increased both runtime and memory and were
removed. Approximate cardinalities, hash-only equality, precomputed answers and
public-seed-specific shortcuts are not valid replacements for exact algorithms.
Further SIMD, PGO, worker-count or layout changes require controlled measurements;
compiler flags alone do not bridge the demonstrated 90-point gap.

## Submission contract

Inputs are read-only under `/data`; write exactly one output under `/out` and
exit zero: `summary.txt`, `results.txt`, `aggregates.txt`, `distances.txt` for A-D.
`TRACK` identifies the track. Builds and runs have no network; pinned images
must remain on the official allowlist. The runtime is unprivileged and read-only,
with 12 threads on six P-cores, turbo off, 8 GB/no swap, 512 processes and 1 GB
of `/tmp`. Output correctness is byte-for-byte SHA-256 equality.

Keep the Board app and team registration configured. At code freeze
(November 2, 15:00 UTC), the repository must be public under an OSI-approved
licence. Those external settings are not established by local correctness tests.
