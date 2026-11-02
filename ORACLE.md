# Speed Demon — Oracle integration protocol

This repository has four concurrent HPC research tracks. The **contest** branch is `speed-demon`; experiments must not mutate it directly. The Oracle coordinates integration but does not have continuous autonomous control of other agents.

## Ownership

| Agent | Allowed source paths | Suggested branch |
|---|---|---|
| A | `track-a/**` | `agent/track-a/<idea>` |
| B | `track-b/**` | `agent/track-b/<idea>` |
| C | `track-c/**` | `agent/track-c/<idea>` |
| D | `track-d/**` | `agent/track-d/<idea>` |
| Oracle | Shared integration files (not submission code) | `oracle/<task>` |

Each experimental pull request must touch **exactly one track**, except an Oracle infrastructure PR, which must not touch any track. Agents may explore multiple ideas, but every candidate must be isolated and should be based on the latest approved `speed-demon` commit. No direct pushes to `speed-demon`, no force pushes, no branch deletions, no automatic merging, and no rebasing somebody else's branch.

## Admission to the integration queue

1. Open a draft pull request targeting `speed-demon`, linking the candidate's exact commit SHA. The PR must state the intended algorithmic change, safety assumptions, and exact files touched.
2. Record reproducible build and validation commands. Run the repository's `check.py` against the official full seed-1 dataset, with the published **expected SHA-256**. Additionally run representative alternative seeds and adversarial input cases. A passing Rust syntax check is **not** equivalent to an official digest validation.
3. Record the baseline SHA, candidate SHA, compiler image, benchmark commands, input seed, process-time distribution (at least 5 repeats), peak RSS, source size, and any regression. Report the complete source and config actually benchmarked, never an uncommitted working tree.
4. The Oracle reviews changed files and dependencies, verifies that another PR is not modifying the same track, and compares this change to the current **approved** per-track best. An independent candidate must not overwrite a previously approved winner.
5. Promotion is **one track at a time**, only after the exact output hash and sandbox constraints pass and a scoring-machine run is checked. Local speedups do not prove a board gain. Re-check the current `speed-demon` head immediately before integration; stop on head changes or conflicts.
6. Record official run URL/ID, exact commit, points, time, memory, and updated score evidence in the relevant `PERF.md` and `BEST_SCORES.md` when actually verified. A lower latency alone may not improve a points score if memory or formatted source size regress.
7. Prefer carefully cherry-picking a reviewed, track-scoped change over merging a branch containing other experiments. Do not use `git push --force` to integrate. Keep rollback commits and the previous source baseline.

## Current recovery point

A historical snapshot was created from `speed-demon` commit `126b40894cb67d44283296176cfaf17f300b2220`:
`oracle/snapshot-20261102-126b408`. This is a **snapshot of the observed branch tip**, not a claim that it was the best-scoring or correctness-verified commit. Do not delete it without a separate decision.

`best-scored-2026-11-01` also exists, but compare and verify its contents before treating it as a known-good full-branch rollback.

## Automated versus manual gates

The `oracle-guard` workflow checks file ownership and Rust compilation. It **does not** automatically prove exact output hashes or official leaderboard score. The full official dataset is deliberately not committed.

**Important:** GitHub Actions and a documented policy do not prevent direct pushes by themselves. An administrator must configure GitHub branch protection for `speed-demon` to require PRs, required check `oracle-guard`, review approval, and disallow force pushes/deletions. Restrict direct pushes if supported. On private repositories, these controls may require a qualifying GitHub plan; the connected integration cannot set them.

The Oracle's judgment is always **fail closed**: missing correctness evidence, missing scoring evidence, unexpected files, non-fast-forward changes, or an unknown baseline means **HOLD — DO NOT MERGE**.
