#!/usr/bin/env python3
"""Compare two Track A Git revisions on the ORIGINAL full public seed-1 dataset.

Builds the pinned production Dockerfile from each Git ref, wraps each image
in a LOCAL process timer, then calls check.py to enforce exact official
input/output SHA-256 on EVERY run. Docker startup is excluded from process
timing. This is not equivalent to the organizer's calibrated scoring CPU.

Usage (after git fetch origin):
  python3 scripts/compare_track_a.py
  python3 scripts/compare_track_a.py --candidate-ref 5bf7f3b971bb6daf4a11da3309cda3816074ed49
  python3 scripts/compare_track_a.py --runs 9
"""
import argparse
from pathlib import Path
import re
import statistics
import subprocess
import sys
from tempfile import TemporaryDirectory

ROOT = Path(__file__).resolve().parents[1]
TIMER_CONTEXT = ROOT / "scripts"
WALL_PATTERN = re.compile(r"SPEED_DEMON_PROCESS_NS=(\d+)")
PEAK_PATTERN = re.compile(r"SPEED_DEMON_CGROUP_PEAK_BYTES=(\d+)")
MINFLT_PATTERN = re.compile(r"SPEED_DEMON_MINOR_FAULTS=(\d+)")


def invoke(args, *, cwd=ROOT, capture=False):
    print("+", " ".join(str(x) for x in args), flush=True)
    result = subprocess.run(args, cwd=cwd, text=True, capture_output=capture)
    if result.returncode:
        if capture:
            print(result.stdout, file=sys.stdout)
            print(result.stderr, file=sys.stderr)
        raise RuntimeError(f"Command failed ({result.returncode}): {args}")
    return result


def source_at(ref, name):
    # Fetch directly from Git without touching any tracked worktree file.
    result = subprocess.run(
        ["git", "show", f"{ref}:track-a/{name}"],
        cwd=ROOT, capture_output=True,
    )
    if result.returncode:
        raise RuntimeError(
            f"Could not read {ref}:track-a/{name}. First run: "
            "git fetch origin speed-demon experiment/track-a-small-groups"
        )
    return result.stdout


def build_revision(ref, label, work_dir):
    context = work_dir / label
    context.mkdir(parents=True, exist_ok=True)
    for name in ("main.rs", "Dockerfile"):
        (context / name).write_bytes(source_at(ref, name))

    # The original Rust builder and Debian runtime are pinned by digest.
    normal_image = f"sd-compare/track-a-{label}"
    timed_image = f"sd-compare/track-a-{label}-timed"
    invoke(["docker", "build", "--network=none", "-t", normal_image, str(context)])
    invoke([
        "docker", "build", "--network=none",
        "-f", str(TIMER_CONTEXT / "track-a-timer.Dockerfile"),
        "--build-arg", f"SOLUTION_IMAGE={normal_image}",
        "-t", timed_image, str(TIMER_CONTEXT),
    ])
    return timed_image


def measure(tag, runs):
    args = [
        sys.executable, str(ROOT / "check.py"), "a", "--skip-build",
        "--image", tag, "--runs", str(runs),
    ]
    result = invoke(args, capture=True)
    print(result.stdout, end="")
    if result.stderr:
        print(result.stderr, file=sys.stderr, end="")
    # Each run must have passed check.py's published output hash.
    passes = len(re.findall(r"PASS\s+Track A run \d+/\d+", result.stdout))
    if passes != runs:
        raise RuntimeError(f"{tag}: only {passes}/{runs} official SHA-256 checks passed")
    wall = [int(v) / 1e6 for v in WALL_PATTERN.findall(result.stderr + result.stdout)]
    mem = [int(v) / 1024**2 for v in PEAK_PATTERN.findall(result.stderr + result.stdout)]
    faults = [int(v) for v in MINFLT_PATTERN.findall(result.stderr + result.stdout)]
    if len(wall) != runs:
        raise RuntimeError(
            f"{tag}: found {len(wall)} process-only timings for {runs} runs. "
            "The timer may not have launched or log output was not captured."
        )
    trimmed = statistics.mean(sorted(wall)[1:-1]) if runs >= 3 else statistics.mean(wall)
    print(f"\n{tag} process-only times (ms): " + ", ".join(f"{v:.3f}" for v in wall))
    print(f"{tag} trimmed process mean: {trimmed:.3f} ms")
    if mem:
        print(f"{tag} maximum observed cgroup peak: {max(mem):.2f} MiB")
    else:
        print(f"{tag} cgroup memory.peak unavailable; memory comparison omitted")
    if len(faults) == runs:
        print(f"{tag} median child minor faults: {statistics.median(faults):,.0f}")
    else:
        print(f"{tag} minor fault counter unavailable")
    return trimmed, max(mem) if mem else None, statistics.median(faults) if len(faults) == runs else None


def measure_interleaved(base_tag, candidate_tag, runs, reverse=False):
    """Alternate one validated baseline and candidate run per pair.

    Every single run invokes check.py's official input AND output SHA-256 gate.
    This avoids testing all baseline runs under one machine-load condition.
    """
    tags = {"baseline": base_tag, "candidate": candidate_tag}
    results = {"baseline": [], "candidate": []}
    names = ["baseline", "candidate"]
    for i in range(runs):
        order = list(reversed(names)) if (bool(i & 1) ^ reverse) else names
        for label in order:
            tag = tags[label]
            result = invoke([
                sys.executable, str(ROOT / "check.py"), "a", "--skip-build",
                "--image", tag, "--runs", "1",
            ], capture=True)
            if not re.search(r"PASS\s+Track A run 1/1", result.stdout):
                raise RuntimeError(f"{label} pair {i + 1}: official output SHA gate not confirmed")
            logs = result.stderr + result.stdout
            times = WALL_PATTERN.findall(logs)
            if len(times) != 1:
                raise RuntimeError(f"{label} pair {i + 1}: expected exactly one process timing")
            peaks = PEAK_PATTERN.findall(logs)
            minor = MINFLT_PATTERN.findall(logs)
            results[label].append((
                int(times[0]) / 1e6,
                int(peaks[0]) / 1024**2 if len(peaks) == 1 else None,
                int(minor[0]) if len(minor) == 1 else None,
            ))
        a, b = results["baseline"][-1][0], results["candidate"][-1][0]
        print(f"pair {i + 1}/{runs}: baseline {a:.3f} ms | candidate {b:.3f} ms", flush=True)

    def summarize(label):
        samples = results[label]
        times = [x[0] for x in samples]
        middle = statistics.mean(sorted(times)[1:-1]) if runs >= 3 else statistics.mean(times)
        peaks = [x[1] for x in samples if x[1] is not None]
        minor = [x[2] for x in samples if x[2] is not None]
        print(f"{label} interleaved process-only times (ms): " +
              ", ".join(f"{x:.3f}" for x in times))
        print(f"{label} trimmed mean: {middle:.3f} ms")
        if len(minor) == runs:
            print(f"{label} median child minor faults: {statistics.median(minor):,.0f}")
        if len(peaks) == runs:
            print(f"{label} maximum cgroup peak: {max(peaks):.2f} MiB")
        return (middle,
                max(peaks) if len(peaks) == runs else None,
                statistics.median(minor) if len(minor) == runs else None)

    return summarize("baseline"), summarize("candidate")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base-ref", default="origin/speed-demon",
                        help="Known-good scored branch or a specific commit")
    parser.add_argument("--candidate-ref", default="origin/experiment/track-a-small-groups",
                        help="Single-change experiment branch or commit")
    parser.add_argument("--runs", type=int, default=5,
                        help="Number of complete official-full-data runs for each (default 5)")
    parser.add_argument("--reverse", action="store_true",
                        help="Measure candidate first to detect run-order/thermal bias")
    parser.add_argument("--interleave", action="store_true",
                        help="Alternate baseline/candidate single-run pairs; best for noisy Docker hosts")
    args = parser.parse_args()
    if args.runs < 1:
        parser.error("--runs must be positive")

    (ROOT / ".out").mkdir(exist_ok=True)
    print("Only official full seed-1 Track A input and exact output SHA-256 are accepted.")
    print("Note: timing is Linux process-only but is NOT the organizer's scoring CPU.")
    with TemporaryDirectory(prefix="sd-compare-a-", dir=ROOT / ".out") as temp:
        directory = Path(temp)
        base_tag = build_revision(args.base_ref, "baseline", directory)
        candidate_tag = build_revision(args.candidate_ref, "candidate", directory)
        if args.interleave:
            baseline, candidate = measure_interleaved(base_tag, candidate_tag, args.runs, args.reverse)
        elif args.reverse:
            candidate = measure(candidate_tag, args.runs)
            baseline = measure(base_tag, args.runs)
        else:
            baseline = measure(base_tag, args.runs)
            candidate = measure(candidate_tag, args.runs)

    print("\n===== REPRODUCIBLE TRACK A COMPARISON =====")
    print(f"base:      {args.base_ref}")
    print(f"candidate: {args.candidate_ref}")
    diff = candidate[0] / baseline[0] - 1
    print(f"Candidate process time: {diff:+.2%} relative to baseline "
          f"({baseline[0]:.3f} vs {candidate[0]:.3f} ms)")
    if baseline[1] is not None and candidate[1] is not None:
        memory_delta = candidate[1] / baseline[1] - 1
        print(f"Candidate peak memory: {memory_delta:+.2%} relative to baseline "
              f"({baseline[1]:.2f} vs {candidate[1]:.2f} MiB)")
    if baseline[2] is not None and candidate[2] is not None:
        print(f"Median child minor faults: {baseline[2]:,.0f} (baseline) vs "
              f"{candidate[2]:,.0f} (candidate)")
    print("Both candidates produced the official expected SHA-256 on every run.")
    print("Do not promote solely on local time: also inspect memory and formatted scc lines.")
    print("Only a new official leaderboard run can establish a score improvement.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError) as exc:
        sys.exit(f"FAIL: {exc}")
