# Speed Demon 2026 — Championship evidence and research guide

**Evidence cutoff:** 2026-11-02 12:47 UTC, before 15:00 UTC code freeze.
**Team:** NewtonRedHeat. **Integration branch:** `speed-demon`.
This is independent research documentation, **not a scored submission
source change**. Everything called "official" below refers to the
organizer's public practice board, never the unseen final holdout.

## Objective and release gate

Optimize the geometric mean of the best three **final track totals**,
not minimum elapsed time alone. The organizer states that the machine
score makes up 65%, human review 35%; judges use code quality (15%),
technical decisions (12%), and reproducible optimization logs (8%).
The measured coefficient is

`K = (T_ref/T)^0.50 (M_ref/M)^0.20 (S_ref/S)^0.15 (D_ref/D)^0.15`

subject to official axis caps and floors. Source and dependency
improvements cannot receive extra credit beyond the reference. Use
actual `scc` formatted-code counts, cgroup memory and pinned full
runtime measurements, not proxy line counts.

**Order:** elimination of work; algorithmic complexity; bytes moved;
data locality; CPU execution; instructions; final compiler tuning.
Every experiment requires (1) a source baseline, (2) one independent
hypothesis, (3) byte-exact independent generator seeds and published
public SHA256, (4) multiple randomized complete-process paired
timings, (5) memory and source-size accounting, (6) official scoring
before permanent promotion, and (7) a precise rollback reference.

## Best observed public-board A-D evidence

Public board generated 2026-11-02 12:30 UTC. Later official reads
at 12:39/12:42/12:46 confirm the same historical best values
([Oracle audit](https://github.com/NewtonRedRing/speed-demon-starter/actions/runs/41447487452)).

| Track | Best public points | K | T | RSS bytes | S | D | Run ID | Best run commit |
|---|---:|---:|---:|---:|---:|---:|---|---|
| A | 84.279208 | 48.482406 | 21.662 ms | 14,987,264 | 451 | 1 | `run-d45aa4ea3d05` | `d660d7e` |
| B | 72.172141 | 27.761494 | 183.883 ms | 46,641,152 | 717 | 1 | `run-f979478c0277` | `c9f733b` |
| C | 74.320728 | 30.648876 | 36.405 ms | 10,354,688 | 712 | 1 | `run-6dcd8ad2012f` | `4b4f425` |
| D | 68.363711 | 23.295618 | 168.724 ms | 76,013,568 | 532 | 1 | `run-f0a2ded6fcfd` | `ae44f84` |

All four best runs passed correctness, sandbox, timeout and build gates.
**Critical distinction:** D's historical best used a two-level wheel,
but retained D source is the independently verified radix heap. The
wheel has a 67.732133 point official regression on another run; do
not say a single wheel high watermark establishes repeatable win.

## Benchmarked hypotheses and Oracle decisions

| Track | Hypothesis | Exactness and complete-run evidence | Decision |
|---|---|---|---|
| A | Double endpoint table from 4K to 8K slots | Official A `run-de44a5ad6c12`: 84.106702; later same source `run-d45aa4ea3d05`: 84.279208, all gates | **Retain** exact 8K/12-worker Rust source |
| A | Table sizes 4K..64K; log partitions 48..192 | [CI 41445114421](https://github.com/NewtonRedRing/speed-demon-starter/actions/runs/41445114421), tiny/sample/full exact, 24 GH runs; top 48 pieces 0.43% faster but much higher one-shot RSS | **Reject**; no official K win established |
| B | Q-FUSE trigger Boolean evaluation | Full output exact; 0.77s baseline vs 1.00s candidate GH | **Reject** |
| B | Borrowed compressed posting iterators | Full exact; +3.5% to +6% slower | **Reject** |
| B | Direct phrase comparisons and hashed long-key table | Full exact; ~neutral or ~1% slower | **Reject** |
| B | Rust vs C dictionary kernel | Same Rust engine plus GCC/Clang C FFI, full exact; Rust 823.5ms, Clang-C 831.9ms, GCC-C 850.9ms GH | **Retain Rust**, not proof that all-C would lose |
| B | Bitmap cutoff 1/64 and LAG=4 | [CI 41445832177](https://github.com/NewtonRedRing/speed-demon-starter/actions/runs/41445832177), full exact; 24 randomized rounds: 677.416 vs 655.581ms; median GH RSS 299,994 vs 302,664 KB | **Official B score-gate trial in progress**; do not claim win |
| C | FALCON-C compact SIMD name-table and exact quote-state parser | Public best `run-6dcd8ad2012f`, 74.320728; exact source blob `e6782b2` | **Retain** |
| C | Batch 8, 64 input chunks | [CI 41445573587](https://github.com/NewtonRedRing/speed-demon-starter/actions/runs/41445573587): 48 randomized GH runs, +0.83% elapsed, -14.5% GH RSS. Official `run-1d9635c77406` 74.154112 vs 74.320728 | **Reject**, restored exact source via `ea2532f` |
| D | Nonempty radix-heap bitmask | [CI 41437938400](https://github.com/NewtonRedRing/speed-demon-starter/actions/runs/41437938400), full reference exact; +2.99% GH runtime | **Reject** |
| D | Fuse reverse degrees into forward remapping | [CI 41444148140](https://github.com/NewtonRedRing/speed-demon-starter/actions/runs/41444148140), full exact; +4.86% runtime and +6.8 MB one-shot RSS | **Reject** |
| D | 128..4096 distance pages; 14..17 graph partition bits | [CI 41444590321](https://github.com/NewtonRedRing/speed-demon-starter/actions/runs/41444590321), exact; best 0.81% faster GH with slightly more RSS | **Reject** unproven K |

The GH machines are **not** the scoring Intel i5-13500, have
different cache/clock/cgroup behaviors, and often disagree with the
official result. A local 2–3% win is not a board-scored gain.

## Sources inspected and actionable invariants

These are **public engineering precedents**, not claims that judges
used these particular algorithms. No third-party implementation was
copied into any scored track.

* **BurntSushi/memchr**:
  https://github.com/BurntSushi/memchr/blob/master/src/arch/x86_64/memchr.rs .
  Exact byte search can amortize feature dispatch and use AVX2, SSE2
  and scalar fallback. In this contest the target ISA is fixed; only
  the internal SIMD comparison and mask-extraction ideas matter.
  A/C already classify structural ASCII with SIMD. Benchmark against
  the existing hot scanner before replacing it.
* **simdjson**, stage-1 structural index:
  https://github.com/simdjson/simdjson .
  Independent quote/escape-state tracking permits wide structural
  classification. Track C has embedded newlines and escaped quotes;
  quote parity must cross SIMD blocks and chunk boundaries exactly.
  A generic JSON parser is **not** a valid CSV replacement.
* **Tantivy**, positional/block posting APIs:
  https://github.com/quickwit-oss/tantivy/blob/main/examples/iterating_docs_and_positions.rs .
  `read_block_postings` permits block-level doc-id iteration;
  positions are requested lazily only when relevant. Track B already
  does selective phrase anchoring; generic decompress-on-every-query
  incurred regressions here. Query sharing must beat all construction
  and iteration costs.
* **SIMD-comp**:
  https://github.com/fast-pack/simdcomp .
  SIMD-friendly compressed integer posting blocks can reduce index
  traffic but require a block size justified by the corpus and the
  250K-document query mix; do not introduce crates or copied code.
* **GAPBS**:
  https://github.com/sbeamer/gapbs .
  Its direction-optimizing BFS and delta-stepping SSSP are high-quality
  research baselines. D's synthetic directed weighted graph has 2,000
  queries and requires exact Dijkstra-like semantics: preprocessing
  must repay itself **within the timed run**, including reverse CSR.
* **Rust radix heap**:
  https://github.com/mpdn/radix-heap .
  Monotone integer priority queues exploit that extracted keys are
  nondecreasing. Track D uses an exact 64-bit radix queue; bitset
  maintenance and bucket wheels were specifically evaluated, not
  assumed to win.
* **Ohad Assulin's Headless Coder SDK**:
  https://github.com/OhadAssulin/headless-coder-sdk .
  Its published interface models adapter-independent threads,
  structured outputs and sandboxed permissions. For this project the
  safe transferable pattern is independent per-track experiments,
  structured benchmark evidence and Oracle-only integrations. The
  package is NOT imported into any runtime submission.

## Judge research — verified facts vs engineering interpretation

**Facts** are from the organizers' published judge biographies:
https://speeddemon.dev/ (Judges). The suggestions after the semicolon
are **inferred from engineering relevance**, not statements that any
individual judge endorses a particular implementation.

1. Karthik Chandrasekaran, Microsoft M365 Copilot query-matching and
   high-QPS indexing; keep Track B query-aware design and explain
   why adaptive inverted lists beat query-triggered execution.
2. Rushikesh Hayatnagarkar, Amazon infrastructure/product and
   multi-agent platforms; present a reproducible offline build and
   simple dependency inventory.
3. Baskaran Jeyarajan, SRE reliability/performance and metrics; show
   per-run medians, variance, correctness, hardware mismatch and
   why failed experiments failed.
4. Karthik Kapula, enterprise automation/CI infrastructure; document
   exact CI tests, branch scopes and deterministic rollback.
5. Lu Zhang, commerce transactions and distributed state-machine
   correctness; make CSV state, output semantics, and overflow
   invariants auditable.
6. Ohad Assulin, AI/software architecture and multiple patents;
   show independently testable algorithm alternatives and provenance,
   but attribute no contest algorithms to him.
7. Prasanna Balasubramani, SoFi/Amazon low-latency transaction
   infrastructure; explain cache layout, memory pressure and
   critical-path instruction tradeoffs.
8. Victoria Drazdova, AI/computer vision sports technology research;
   avoid claiming her personal optimization preferences; provide
   reproducible and legible empirical results.
9. Ramanjaneyulu Mallisetty, enterprise software architecture and
   patents; record exactness checks, reviewable unsafe boundaries and
   source-size impact.
10. Sushrutha Sreevathsa, Yahoo SRE and operational analytics;
    provide reliable board run IDs and identify unstable or
    environment-sensitive measurements.

**Do not optimize for a particular person's inferred taste.** The
published score rubric is the contract.

## Finalization risks and checklist

- Current GitHub repository reported **private** as of 12:47 UTC;
  organizers require **public repository within one hour after
  15:00 UTC freeze**. Visibility changing is an administrative action
  not exposed to the connected GitHub tools; the owner must perform
  it. Repo has an MIT LICENSE at root.
- Each `track-[a-d]/.speed-demon.toml` declares Rust, 0 direct
  runtime dependencies, team NewtonRedHeat. Each Dockerfile uses
  digest-pinned base images and compiles without downloading crates;
  B/C generate an **independent build-time sample** for PGO.
- Source has official output-hash and sandbox score evidence;
  agent-side test runners are **additional** validation only.
- Preserve exact approved source blobs, especially Track C
  `e6782b2`, Track D `46bfcda`, Track A `4559a57`.
- Score-gated Track B trial uses branch-merged source blob
  `3b1c024`. Never retain it merely from GH benchmark timing.
- Before freeze, check current `speed-demon` SHA, ensure all A-D
  Dockerfile/manifest/output paths are present, confirm full public
  correctness in board feed, record best per-track runs and selected
  source SHA, then stop speculative writes.
- Deadline 2026-11-02 **15:00 UTC / 16:00 CET**;
  only the organizer's recorded commit affects the holdout.
