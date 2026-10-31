# Repository consolidation audit — October 31, 2026

Scope: `NewtonRedRing/speed-demon-starter`; tracked default branch
`speed-demon`. This audit covers the **12 branch refs and 10 PRs** observed
before cleanup. The repository was reviewed for exact output, end-to-end
performance evidence, supersession, and risk of replacing newer algorithms.

## Actions actually completed

1. PR #10 was **merged via squash**. The exact research report and
   independent Track B phrase-anchor prototype are preserved under
   `research/`. No scoring executable source was changed by this merge.
   PR #10 head: `49c5b1506ca98d4fdf238e2d82d7f107c1210a61`;
   merge commit: `e714f0b1b0dcdcd2e5f90da25178317d6952bd6f`.
2. Selected `scripts/public_full_benchmark.py` from PR #1 was copied
   byte-identically into `speed-demon` at commit
   `f1e0681f0d75ee615248670a5aa0edbe108a35c2`.
   The script supports fetching/verifying official public input checksums
   and checking each Docker output against the manifest. Its use still
   requires Python 3.13, Docker, downloaded data and local execution.
3. PRs #1, #2, #3, #4, #5, and #6 were **closed without merging**
   unverified implementation replacements. Each includes a conversation
   comment explaining the decision.
4. PRs #7, #8, and #9 remain open as drafts. They represent the latest
   still-unverified experiments for B, C and D, respectively.
5. The 8 audited stale branches are prepared for deletion with
   `scripts/prune_stale_branches.sh`. The linked GitHub connector does
   not expose branch-ref deletion, so **no branch refs were actually
   deleted from this chat**. The script is dry-run by default and requires
   an explicit `--execute`, checks that `origin` targets this repository,
   and uses force-with-lease pinned to each audited SHA.

## Why no experimental scoring code was merged

| PR | Changed score code | Assessment | Outcome |
|---|---|---|---|
| #1 | A/B/C/D | All four implementations older than current default; CI never completed; own research record explicitly says full integration unvalidated | Close; preserve official benchmark script |
| #2 | A | Deferred-count/PGO attempt is superseded by current six-byte/SWAR/AVX2/hour-local A; CI failed | Close |
| #3 | B | Short-token direct map retained in newer experiment PR #7 | Close redundant parent |
| #4 | C | Mapped CSV parity retained in newer experiment PR #8 | Close redundant parent |
| #5 | D | Mapped CSR parser retained in newer experiment PR #9 | Close redundant parent |
| #6 | A | Fused-hash experiment is outpaced in architecture and full-input local evidence by default A | Close |
| #7 | B | Hot-token lookup + custom hash has only isolated synthetic microbenchmarks | Keep draft, require official full test |
| #8 | C | Mapped parity + region loading has only isolated synthetic microbenchmarks | Keep draft, require official full test |
| #9 | D | Mapped parser + radix frontier caching has no complete end-to-end score validation | Keep draft, require official full test |
| #10 | Research | No timed code changed; reproducible exact anchor prototype and research report | Merge |

## Protected/current baseline

Default `speed-demon` already contains the A speed-first implementation
with six-byte records, fused SWAR/CRC32C parsing, AVX2 sorting, exact
nearest-rank quantiles, hour-local scatter and right-sized output.
Its PERF log reports a *local*, controlled median
50.48445 -> 40.34590 ms and cgroup peak 69.53 -> 36.39 MiB.
This is **not** a new official leaderboard run.

B current default: mapped corpus scan, exact Aho-Corasick and compact
sparse/dense Boolean postings. C: quote-safe CSV parser and striped
exact-name dictionary. D: packed bidirectional CSR and exact 64-bit
radix-queue Dijkstra. Replacing any of these with the older PR #1
algorithms would be a high regression risk.

The historical GitHub Actions runs for PRs #1 and #2 are recorded as
failed; they are not evidence of correctness, speed or official scores.

## Post-cleanup desired branches

**Keep:**
- `speed-demon` (default; production scoring source)
- `experiment/track-b-speedfirst-inline-hash` (open draft PR #7)
- `experiment/track-c-speedfirst-fast-region` (open draft PR #8)
- `experiment/track-d-speedfirst-frontier-cache` (open draft PR #9)

**Prune after checking SHA leases** (all their PRs are closed or merged):
- `experiment/track-a-speed-first` — identical to default at the start of the audit
- `experiment/track-a-speed-first-deferred-count` — closed PR #2
- `experiment/track-a-speedfirst-fused-scan` — closed PR #6
- `experiment/track-b-speedfirst-short-token` — closed PR #3
- `experiment/track-c-speedfirst-parity-mmap` — closed PR #4
- `experiment/track-d-speedfirst-mapped-graph` — closed PR #5
- `experiments/90-plus-exact-20261030` — closed PR #1
- `research/max-throughput-four-tracks-20261031` — merged PR #10

Source commits remain accessible from the closed/merged PR timelines.

## Deleting the stale remote branch refs

In a local authenticated Git clone of this repository:

```bash
git fetch origin
bash scripts/prune_stale_branches.sh             # inspect the dry run
bash scripts/prune_stale_branches.sh --execute   # perform SHA-leased deletion
```

The script refuses to delete if a branch has been updated since this
audit, guarding against concurrent work. Do not remove the three active
experimental branches until their exact-output and full-score tests finish.

## Next evidence gates for PRs #7–#9

- Rebuild under the pinned offline toolchain; no unapproved dependencies.
- Run random and hostile-edge-case oracle tests plus ASan/UBSan.
- Check all official public full input/output SHA-256 digests.
- Alternate whole-process performance measurements against the
  exact current `speed-demon` branch, with cgroup v2 `memory.peak`.
- Include formatter-counted code lines and dependency weight when
  comparing the official `K` coefficient.
- Merge only a demonstrated whole-score gain. Record its commit and
  official run ID; otherwise close/reject the experiment.
- There is **no verified new 90-point or first-place outcome** in this audit.
