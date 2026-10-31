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

### Retained implementations

Each track has exactly one source file, `src/main.cpp`; rejected alternatives,
profiling copies and backup implementations are not part of any build context.
Keep the pinned Dockerfile, metadata and approach/performance documentation with
that source. The validation scripts and tests stay outside the scored tracks.
Old implementations remain available in Git history, not as extra scored files.

The retained versions are selected from the tested candidates, not claimed to
be globally optimal. In particular, C retains owned string/deque entries rather
than the larger instrumented arena-backed experiment. Its latest changes are
locally validated but do not yet have an official score.

Latest full public seed-1 rechecks (A: eight process-timed paired runs;
B/C/D: five process-timed runs):

| Track | Median time | Peak RSS | Official output hash |
|---|---:|---:|---|
| A | 0.040346 s | 34.41 MiB | PASS |
| B | 0.509163 s | 278.18 MiB | PASS |
| C | 0.113996 s | 21.89 MiB | PASS |
| D | 0.394181 s | 130.77 MiB | PASS |

These are local measurements, not leaderboard scores or controlled comparisons
with previous batches. All four also passed `check.py` with the live manifest's
expected hashes, allowlisted offline builds and the production-style sandbox.
Each benchmark run checks that `/out` contains exactly its required output file.
All 15 regression tests passed. No dependency downloads are needed at build or
run time.

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

## Algorithms and historical local milestones

| Track | Approach | Previous local median | Candidate median | Candidate peak RSS |
|---|---|---:|---:|---:|
| [A](track-a/README.md) | Parallel mapped parsing, fused hour scatter/selection, direct output buffers | 0.541742 s | 0.042965 s | 66.39 MiB |
| [B](track-b/README.md) | Token phrase automaton, compact sparse/bitmap Boolean postings | 5.749076 s | 0.834935 s | 202.97 MiB |
| [C](track-c/README.md) | Quote-safe parallel CSV, pooled exact names, overlapped region lookup | 3.299495 s | 0.098435 s | 14.70 MiB |
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

Track C's latest investigation measured an 8.3% median time reduction in a
same-sandbox alternating comparison against its published implementation.
Its latest recorded official result remains 62.989248 points; the new local
candidate has no official score. At that published memory/source size, 80 points
would require about 21.45 ms, roughly 4.79x faster than its 102.775 ms official
run. The dominant measured stage is CSV parsing plus exact-name aggregation.

Track A's subsequent speed-first redesign measured 0.070378 s before versus
0.046220 s after in twelve alternating same-sandbox runs per executable:
34.3% lower elapsed time. Peak RSS increased from 41.01 to 68.10 MiB.
The retained implementation removes per-group latency allocations and uses
hour-major parallel prefix/scatter storage. A later fused hour pipeline reduced
the preceding candidate's controlled median another 3.2%, from 0.043447 s to
0.042055 s, and increased measured mean CPU concurrency from 7.41 to 7.72
CPU-equivalents. Neither continuous twelve-thread saturation nor equivalent
target-machine hardware is established locally. These speed improvements are local;
memory/source penalties still affect points, and no new official score exists.

### Earlier all-track speed-first tuning

B now scans mapped corpus shards without per-document `getline` copies and
summarizes dense Boolean words with exact weighted population counts. C clears
its decode buffer only for escaped names and releases consumed pages in larger,
page-safe batches. D parses complete buffer blocks in one pass and prunes
relaxations that cannot improve the current shortest path.

Eight alternating runs per executable in one sandbox, using only official
full seed-1 performance inputs and live manifest hashes:

| Track | Before (s) | Retained (s) | Median time reduction | Before / retained maximum RSS (MiB) |
|---|---:|---:|---:|---:|
| B | 1.238990 | 1.204293 | 2.8% | 202.94 / 200.67 |
| C | 0.132686 | 0.128629 | 3.1% | 14.48 / 21.73 |
| D | 0.459892 | 0.436360 | 5.1% | 138.95 / 136.98 |

These compare against the start of this all-track tuning pass, not the much
older original submissions. Host load increased between batches; the higher
absolute times must not be compared against earlier isolated runs. C's extra
resident pages are an intentional raw-speed tradeoff, not a points improvement.
Mapped B improved in two controlled comparisons; a byte-classification table,
deferred C deduplication and larger D construction layouts were rejected.
Unused includes and experiment-only statements were removed; required error
reporting, exact comparisons, metadata and documentation remain. Removing
comments or documentation alone does not speed execution.

A subsequent source cleanup removes A's custom hash adapter in favor of the
standard string hash and its redundant probe counter: the existing 1,842-entry
limit prevents filling the 4,096-slot table. C no longer computes the final
shard's unused quote parity, launches an extra prepass worker, or constructs a
scratch string for notes that are never decoded. Its unused integer alias and
D's unused string include were removed. All remaining functions in A-D serve
the contest algorithms, resource management or explicit error reporting.

All 14 regression tests, all eight solution methods under ASan/UBSan, strict
warnings and all four allowlisted offline production-sandbox checks pass for
the retained sources. No new board score is available.

### Latest profile-guided tuning pass

Temporary phase timers on the current official inputs found 26-43 ms in A
parsing, 650-703 ms scanning B plus 137-158 ms merging, 88-98 ms parsing and
aggregating C, and 229-252 ms answering D queries. These are elapsed phase
ranges, not isolated CPU profiles. All timers were removed.

Retained changes: A hashes endpoint bytes inline while retaining full equality;
B allows twelve allocator arenas instead of four and frees independent worker
posting storage in parallel after merging; C specializes copied-name versus
length-only note parsing at compile time; D packs radix items into twelve bytes
and does not zero-initialize edge storage that is completely filled before use.

Latest eight-run alternating comparisons per executable, against the validated
versions at the start of this pass:

| Track | Before (s) | Retained (s) | Median time reduction | Before / retained maximum RSS (MiB) |
|---|---:|---:|---:|---:|
| A | 0.054506 | 0.052878 | 3.0% | 66.92 / 66.35 |
| B | 1.230033 | 0.949081 | 22.8% | 197.38 / 219.41 |
| C | 0.125930 | 0.121983 | 3.1% | 22.55 / 21.52 |
| D | 0.464529 | 0.450791 | 3.0% | 135.01 / 133.96 |

These are additional local raw-time reductions, not cumulative percentages or
new leaderboard scores. B trades approximately 22 MiB for lower allocation
contention; memory and formatted-source penalties can offset speed gains.
A/C/D gains are modest and shared-host-sensitive. Separate five-run measurements
are in the latest recheck table above and are not the controlled comparisons.

Rejected experiments were removed: B token hashing, C's per-worker name cache,
inline name hashing and smaller dictionary, and D's speculative prefetch and
early own-distance lookup. Full official hashes, all 14 tests, all eight
solution methods under ASan/UBSan, strict warnings and all four offline sandbox
checks passed again. A's oracle now checks every endpoint length from 1 to 125.

### Track B follow-up: remove incremental posting construction

A complete B source review identified approximately 36.3 million corpus tokens
and 21.1 million Boolean memberships. The published implementation spent about
709-730 ms scanning plus 120-127 ms merging in diagnostic runs. Output was only
4-5 ms, so formatting was not the main problem.

B now collects hits into contiguous per-worker vectors, prefix-scatters into
worker-owned posting slices, and builds each final sparse vector or bitmap once
in parallel. Incremental `Posting::add`, `promote` and `merge` are no longer
needed and were removed. Exact phrase matching, full token equality and
left-to-right Boolean semantics are unchanged.

Latest eight-run alternating comparison: **0.786195 s before versus
0.625108 s after, 20.5% lower median runtime**. Peak RSS increased from
219.28 to 280.30 MiB. Two earlier controlled batches confirmed 20.5-21.8% gains.
The separate five-run B recheck appears above; cross-batch timings are not
speedup evidence. All 14 tests, B's sanitizer oracles, strict warnings and its
offline production sandbox pass. No new official score is available.
The [B performance log](track-b/PERF.md) records the source audit, rejected
experiments and memory tradeoff.

### Experimental Track A follow-up

Developed on `experiment/track-a-speed-first`, the validated candidate is also
promoted to the submitted `speed-demon` branch. A now uses six-byte records, deferred
hour-local counts/scatter, fused SWAR/CRC endpoint parsing, SWAR decimal parsing
and exact small-group AVX2 sorting. All latency bits and full endpoint equality
are preserved.

The latest eight-run alternating comparison measured **0.05048445 s before
versus 0.04034590 s after, 20.1% lower median runtime**. Median fresh-cgroup
`memory.peak` dropped from 69.53 to 36.39 MiB, **47.7% lower**. Input pages were
prewarmed by another live cgroup; a small static measurement supervisor is
included in the counter. This is not RSS, an official harness result, a
verified 90-point score, or proof of scoring-CPU saturation.

All 15 tests, three A sanitizer oracle methods, strict AVX2-only compiler
checks, two additional official-generator sample seeds against the original
reference, and the full-data production sandbox pass. Slower streaming,
sixteen-lane sorting, six/eight-worker and oversized-reservation variants were
removed; offline two-seed PGO showed no reliable gain and was also removed.
The [A performance log](track-a/PERF.md) records individual experiments.

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
- Nine solution-oracle methods pass; six dataset-integrity tests check
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
The later official feed generated `2026-10-30T19:32:05Z` confirms successful
scored runs for published commit `dfdc930`:

| Track | Official time | Official memory | Official points |
|---|---:|---:|---:|
| A | 0.078022 s | 52.05 MB | 66.618152 |
| B | 0.806407 s | 205.30 MB | 50.651659 |
| C | 0.109505 s | 17.04 MB | 62.331786 |
| D | 0.418953 s | 141.78 MB | 57.008935 |

These establish improvement over the historical result, but not first place
or 90 points. Actual run IDs are recorded in the per-track performance logs.
The feed at `2026-10-30T19:39:41Z` places NewtonRedHeat at **61.861 overall,
rank 20**; its leader is at 73.212 overall.
Subsequent local tests rejected lower worker counts, alternate index merges,
singleton postings and smaller CSV hash buckets when their speed, memory and
source-size tradeoffs did not improve the score. A modest small-group sorting
candidate for A was published in `1ba1831`; the table above still describes
`dfdc930`, not that later commit.
All 15 current software tests pass, including the new small-group threshold
cases, and the changed A source passes strict compiler warnings.

## Remaining optimization work

| Track | Remaining cost | Next evidence needed |
|---|---|---|
| A | Endpoint hashing/parsing and remaining output/selection work | CPU profile of the contiguous-storage design on scoring-equivalent hardware |
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
