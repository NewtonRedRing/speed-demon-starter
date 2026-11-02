# Track D — experimental C implementation (NOT PROMOTED)

This branch contains a C11 alternative in candidate.c. The production
main.rs, standard Dockerfile, submission metadata, and speed-demon
submission are **unchanged**.

## Design

- Two-pass scan of the official shuffled graph.txt; builds forward
  and reverse CSR directly without an intermediate edge-tuple array.
- Each CSR stores packed 32-bit target and 11-bit weight fields at full
  scale. Weights >= 2047 use sorted exact exception tables; zero, huge
  weights, parallels, self-loops and disconnected nodes are preserved.
- Up to 12 pthread workers perform exact bidirectional Dijkstra using
  monotone 65-bucket radix heaps and generation-tagged sparse distance maps.
- Answers remain in original query order; -1 for unreachable, exact
  decimal 64-bit values otherwise. Zero non-stdlib runtime dependencies.

This candidate tests whether eliminating the existing Rust implementation's
BFS locality-remapping stage makes up for slower cache-random query searches.
It may be slower; do not promote from intuition.

## Build candidate without changing scored submission

The separate Dockerfile.candidate uses the organizer-allowlisted pinned
gcc:15 and Debian 13 images. From inside the track-d folder:

    docker build --network none -f Dockerfile.candidate -t speed-demon-d-c .

For local tests:

    cc -std=c11 -O3 -march=native -flto -pthread -Wall -Wextra -Werror -o candidate candidate.c
    ./candidate /path/to/input /path/to/existing/out/distances.txt

For a formal full-scale check, temporarily substitute Dockerfile.candidate
for Dockerfile in an isolated disposable worktree, then invoke check.py
against official data and the published expected SHA-256. Never swap the
production build until exact output, sandbox, held-out style seeds and
official score K are verified.

## Local evidence

- gcc 14.2 -O3 -march=native -flto -pthread -Wall -Wextra -Werror: PASS.
- Official worked-example SHA-256:
  2e9871bba1d5013144ceff48d806a28da0e3f203b29118a89d98d8f654c3f6a0 — PASS.
- Python heapq oracle: 96 independent random graph tests plus a
  6,000-node/60,000-edge/203-query differential test — PASS.
- Non-official synthetic smoke (174,051 nodes, 500,000 edges, 200 queries):
  0.26 seconds, 30,064 KB peak RSS on a different environment.
  This has neither the official generator's query distribution nor
  the judging CPU, so it is NOT a comparison with the Rust baseline.

Full public-seed official hash, Docker GCC 15/sandbox, and official score:
NOT CHECKED. **Decision: HOLD / DO NOT MERGE**.

The scored Rust source remains the approved baseline. Historical D best:
68.363711 (experimental wheel); verified stable radix 68.316671.
