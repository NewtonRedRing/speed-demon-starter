#!/usr/bin/env python3
"""Check that an HPC PR changes one track, or only Oracle control files.

This is a scope + compile helper, NOT an exact correctness or speed oracle.
Usage:
    python3 tools/oracle_guard.py --base <commit> --head <commit> --branch <PR head name>
"""
import argparse
import os
import subprocess
import sys

ORACLE_FILES = {
    "ORACLE.md",
    ".github/workflows/oracle-guard.yml",
    ".github/pull_request_template.md",
    "tools/oracle_guard.py",
}
TRACKS = "abcd"


def changed_paths(base, head):
    proc = subprocess.run(
        ["git", "diff", "--name-only", "--no-renames", "-z", f"{base}...{head}"],
        capture_output=True, check=True,
    )
    return [os.fsdecode(p) for p in proc.stdout.split(b"\0") if p]


def check(paths, branch):
    if not paths:
        raise ValueError("No changed files in PR")
    if branch.startswith("oracle/"):
        unexpected = [p for p in paths if p not in ORACLE_FILES]
        if unexpected:
            raise ValueError(f"Oracle PR cannot modify submission or unapproved shared files: {unexpected}")
        return "oracle"
    touched = {p.split("/", 1)[0] for p in paths}
    if len(touched) != 1:
        raise ValueError(f"PR changes multiple paths: {sorted(touched)}; one track per PR")
    single = next(iter(touched))
    if single not in {f"track-{t}" for t in TRACKS}:
        raise ValueError(f"Non-Oracle PR touches shared path {single!r}")
    track = single[-1]
    prefix = f"agent/track-{track}/"
    if branch.startswith("agent/track-") and not branch.startswith(prefix):
        raise ValueError(f"Agent branch does not match modified track {track}: {branch}")
    if any(not p.startswith(single + "/") for p in paths):
        raise ValueError("Unexpected file outside assigned track")
    return track


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", required=True)
    ap.add_argument("--head", required=True)
    ap.add_argument("--branch", required=True)
    ap.add_argument("--github-output", default="")
    args = ap.parse_args()

    try:
        paths = changed_paths(args.base, args.head)
        track = check(paths, args.branch)
    except (ValueError, subprocess.CalledProcessError) as exc:
        print(f"ORACLE HOLD: {exc}", file=sys.stderr)
        return 1

    print(f"ORACLE SCOPE PASS: {track}; files: {', '.join(paths)}")
    if args.github_output:
        with open(args.github_output, "a", encoding="utf-8") as output:
            output.write(f"track={track}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
