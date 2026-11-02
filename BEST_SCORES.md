# Speed Demon 2026 — verified best public scores

Team: `NewtonRedHeat`. These are the historical **best per-track** official board
runs as confirmed by official public-board feed through **2026-11-02 12:50:20 UTC**,
independently recorded in
[the official scoring audit](https://github.com/NewtonRedRing/speed-demon-starter/actions/runs/41437999219).
The current submission source for each track was separately compared against
the corresponding source in GitHub. Runs on the merged branch can score
differently due to timing/memory variance. The final holdout determines final
results.

| Track | Best public-board commit | Points | Runtime | Peak memory | Official gates |
|---|---|---:|---:|---:|---|
| A — Ingest | `d660d7e49f4eca6a5673ca92a96463b1db238359` | **84.279208** | **21.662 ms** | 14,987,264 bytes | passed |
| B — Index | `c9f733bb7849269b600358483cc1c06936318bb6` | **72.172141** | **183.883 ms** | 46,641,152 bytes | passed |
| C — Join | `4b4f4259eb78d55be5f835337488a05c7add6ac6` | **74.320728** | **36.405 ms** | 10,354,688 bytes | passed |
| D — Traverse | `ae44f84fab34eada896a4276996f792949b31e83` | **68.363711** | **168.724 ms** | 76,013,568 bytes | passed |

## Verified source selection on speed-demon

After the 2026-11-02 11:38 UTC official A hash-table sizing run,
`speed-demon` held scored source at `0df911467a16a9e3f13f1192dd87ee786f69f2e2`. The official
best-score feed below reflects individual historical runs, not a single
four-track run. The exact
`main.rs` blob SHA-1s for that branch were checked against GitHub refs:

| Track | Submission main.rs blob | Identical to scored reference |
|---|---|---|
| A | `4559a57b037785452dcf64f4e098456440f96cd8` | `0df9114` — HARP + 8,192-slot endpoint interner |
| B | `c7dc80d3413beb6d0303bc376fe95e816a2b7b1c` | `c9f733b` and `126b408` |
| C | `e6782b241cd8f07bca5b114fdb05019a483f91e7` | `c0053b4` and `63ec07b` — FALCON-C |
| D | `46bfcda6d77a5bfb6a72a4baf108de4d71c81a1c` | `6759b53` — restored historical verified best |

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

## November 2, 11:20 UTC verified-machine-score update

The official public board feed recheck at [GitHub Actions run 41436015800](https://github.com/NewtonRedRing/speed-demon-starter/actions/runs/41436015800) recorded the latest best A run as **84.036874** on commit `9bb3f6a`, and the best C as **73.965629** on commit `c0053b4`. Their source files match the retained implementations; these are timing variance across identical source versions, not claimed new changes. Best B remained **72.172141**.

**D regression and correction:** A novel two-level exact wheel queue was locally about 7.25% faster, but the official board run of `9bb3f6a` scored **67.732133**, **177.210 ms**, 77,762,560 B and 532 source lines, versus historical `6759b53` at **68.316671**, **174.011 ms**, 76,247,040 B and 485 source lines. All gates passed, but the candidate was worse on every measured score axis on that board test. Its Track D source was therefore replaced with the exact officially verified radix-heap source in commit `635c41c`. No cross-track code was changed.

A new code candidate should not be promoted solely because local GitHub Actions timing improves: require the full official K / points outcome, plus correctness and source scope review.

## November 2, 11:38 UTC: score-verified Track A endpoint interner

Test branch `experiment/track-a-hash8k-20261102` retained the exact HARP
parser, Rust-only implementation, zero dependencies, and all other tracks
unchanged. It doubled the endpoint-interner hash-slot count from 4096 to 8192
while keeping the exact 2048-name ID and arena capacity. The official full
public SHA and tiny seeds 2, 7, 23, 42, 2026, plus sample 17, passed.
GitHub Actions paired whole-process timings also favored the 8192-slot table
(median base/candidate ratio **1.02546**, 25/38 wins, bootstrap 95% interval
[1.00016, 1.04255]).

The only source candidate was pushed to `speed-demon` as `0df9114` for
official contest validation. [Independent official board audit
41437999219](https://github.com/NewtonRedRing/speed-demon-starter/actions/runs/41437999219)
confirmed **all gates passed**, run `run-de44a5ad6c12`, with **84.106702
points**, **21.706 ms**, **15,515,648 bytes**, **451 canonical source lines**.
The earlier identical-source baseline best was **84.036874**, **22.229 ms**,
**14,880,768 bytes**, **450 source lines**. The new design therefore
improved runtime by approximately **2.35%** and official points by
**0.069828** despite a modest peak-memory increase.

The other Track B compressed-membership and Track C quote-free fastpath
experiments were not promoted: their local wins were not supported by a
credible overall score improvement. The Track D BFS locality-blob sweep
was effectively neutral over tested widths and was also not promoted.


## November 2, 11:38 UTC: later official-score cross-check

[Public board audit run 41438445116](https://github.com/NewtonRedRing/speed-demon-starter/actions/runs/41438445116)
established higher **historical single-run** values than earlier 11:20 snapshots:
Track C **74.320728** on `4b4f425` (36.405 ms), and Track D **68.363711**
on `ae44f84` (168.724 ms). The Track C source blob matches the current
`speed-demon` code, so no additional C merge is required.

**Track D caveat:** That higher D run is the *integer-wheel* source (532 formatted
lines), whereas the currently retained Track D source is the older 485-line
radix heap. Another official wheel run (`9bb3f6a`) scored **67.732133** and
a scored radix run (`6759b53`) scored **68.316671**. The high watermark alone
does not establish a repeatable improvement, particularly because end-to-end
GitHub-hosted results favored the wheel while some official runs did not.
The coordinator therefore preserves the radix version pending stronger
official scoring evidence. The table above records historical best individual
runs, **not** necessarily the source in the submission branch or a single
four-track run.


## November 2, 12:50 UTC — final score-gate selection audit

The latest scoreboard snapshot was obtained independently in
[Actions 41447919837](https://github.com/NewtonRedRing/speed-demon-starter/actions/runs/41447919837).
The historical verified A best was already **84.279208**,
`run-d45aa4ea3d05`, `K=48.482406`,
21.662 ms, 14,987,264 bytes. It uses the same
`track-a/main.rs` blob SHA
`4559a57b037785452dcf64f4e098456440f96cd8`
as the currently restored 12-worker HARP parser.

**B:** A reversible, exact two-constant bitmap cutoff=64 / scanner LAG=4
trial scored **71.521322 points**, 182.235 ms,
55,414,784 bytes, S=717, D=1, K=26.941790,
run `run-fb448cadcb4e` (source introduced in `4168def`).
Despite slightly faster wall time, much higher official memory
reduced K below approved B best **72.172141 points, K=27.761494**,
`run-f979478c0277`. Oracle revert
`e2b1126f3066160af8ca82461cdd0aa06e1b5de6`
restored blob `c7dc80d3413beb6d0303bc376fe95e816a2b7b1c`.

**C:** A reversible exact batch8 / 64 chunks trial scored
**74.154112 points**, 37.009 ms, 10,326,016 bytes,
K=30.414610, `run-1d9635c77406`, below
the approved best **74.320728, K=30.648876**,
`run-6dcd8ad2012f`. Oracle revert
`ea2532f68462ca5fc07c3e93cf28d62d80a163c1`
restored FALCON-C blob
`e6782b241cd8f07bca5b114fdb05019a483f91e7`.

**D:** Historical two-level wheel high mark
**68.363711 points** / 168.724 ms,
`run-f0a2ded6fcfd`, must be distinguished from
the final code: the current radix-heap blob
`46bfcda6d77a5bfb6a72a4baf108de4d71c81a1c`
was chosen for repeatability and the independently scored
**68.316671** radix best (`6759b53` source) must not be
misrepresented as the latest wheel run. Conflicting
wheel public runs included **67.732133**.
All these are *public seed-1 practice results*,
not final holdout results.

**Final integration rule:** the retained source in A/B/C/D
is byte-exact to historically official-scored, all-gates-passing Rust
implementations. Research/Oracle PRs and unverified alternatives are
not part of submission. Exact SHA checks, no runtime dependencies,
offline digest-pinned Dockerfiles and truthful per-track PERF.md
histories are retained. The GitHub repository must be made public
within the organizer's required post-freeze window.
