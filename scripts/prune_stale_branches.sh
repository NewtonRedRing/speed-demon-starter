#!/usr/bin/env bash
# Safe branch cleanup for the 2026-10-31 consolidation audit.
# Dry run (default): bash scripts/prune_stale_branches.sh
# Execute with authenticated git: bash scripts/prune_stale_branches.sh --execute
#
# Every deletion uses force-with-lease pinned to the audited exact SHA.
# If another person pushed to a branch, deletion MUST fail.
set -euo pipefail

if [[ "${1:-}" != "" && "${1:-}" != "--execute" ]]; then
  echo "Usage: bash scripts/prune_stale_branches.sh [--execute]" >&2
  exit 2
fi

url=$(git remote get-url origin)
case "$url" in
  *github.com/NewtonRedRing/speed-demon-starter|*github.com/NewtonRedRing/speed-demon-starter.git|*github.com:NewtonRedRing/speed-demon-starter|*github.com:NewtonRedRing/speed-demon-starter.git) ;;
  *) printf 'Refusing: origin is not NewtonRedRing/speed-demon-starter: %s\n' "$url" >&2; exit 1 ;;
esac

while read -r branch expected; do
  [[ -n "$branch" ]] || continue
  remote_sha=$(git ls-remote --heads origin "refs/heads/$branch" | awk 'NR==1 { print $1 }')
  if [[ "$remote_sha" != "$expected" ]]; then
    echo "SKIP: $branch (not at audited SHA, or already deleted)" >&2
    continue
  fi
  if [[ "${1:-}" == "--execute" ]]; then
    echo "Deleting audited stale branch: $branch"
    git push "--force-with-lease=refs/heads/$branch:$expected" origin ":refs/heads/$branch"
  else
    echo "DRY RUN: would delete $branch at $expected"
  fi
done <<'STALE_HEADS'
experiment/track-a-speed-first 7d0b07346df55c0dd9d5c860928253f4e7554c53
experiment/track-a-speed-first-deferred-count 14ae6483a055caf63086f4d2720321c0e3be9c87
experiment/track-a-speedfirst-fused-scan 0e8c03d0c6527bc67e42a969f59d8af58afba14f
experiment/track-b-speedfirst-short-token ce02888b0d1000acebd64c055e287108c862cb58
experiment/track-c-speedfirst-parity-mmap 0dc16a0b86d90595e94a16e82df365548a4d97ee
experiment/track-d-speedfirst-mapped-graph 1799a24b314944773f6608f6ef54639d4a6c3b45
experiments/90-plus-exact-20261030 9da899dbeb37ba9ec0f4ce04d2ae54cf87d8ba1e
research/max-throughput-four-tracks-20261031 49c5b1506ca98d4fdf238e2d82d7f107c1210a61
STALE_HEADS

if [[ "${1:-}" != "--execute" ]]; then
  echo
  echo "No branches were deleted. Pass --execute to apply the checked deletions."
fi
