#!/usr/bin/env python3
"""Search 100 exact-output-checked build configurations per Speed Demon track.

Run from an authenticated checkout with Docker and verified *official* data:

  python3 scripts/fetch_data.py
  python3 scripts/optimize_100.py --tracks a b c d --engine docker
  python3 scripts/optimize_100.py --plan
  python3 scripts/optimize_100.py --tracks a b c d --engine docker --apply

--apply updates only the experimental branch's Dockerfile build arguments
after a full-output SHA check, repeated validation and a 1% proxy-margin.
It does NOT commit, push, or claim an official leaderboard result.
"""
import argparse
from contextlib import contextmanager
import hashlib
import itertools
import json
import math
import os
from pathlib import Path
import re
import resource
import statistics
import subprocess
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = {"a": "summary.txt", "b": "results.txt",
          "c": "aggregates.txt", "d": "distances.txt"}
INPUTS = {"a": ("requests.log",), "b": ("docs.txt", "queries.txt"),
          "c": ("records.csv", "regions.csv"),
          "d": ("graph.txt", "queries.txt")}
TUNE = {
    "a": (256, 384, 512, 768, 1024),  # per-hour reserve divisor
    "b": (8, 16, 32, 64, 128),        # Boolean term merge batch
    "c": (1024, 2048, 4096, 8192, 16384),  # name stripe buckets
    "d": (64, 128, 256, 512, 1024),   # graph read buffer KiB
}
BASELINE_PARAM = {"a": 512, "b": 32, "c": 4096, "d": 256}
WORKERS = (6, 8, 10, 12)
STYLES = {
    "base": (),
    "unroll": ("-funroll-loops",),
    "no-vectorize": ("-fno-tree-vectorize",),
    "no-ipacp": ("-fno-ipa-cp",),
    "O2": ("-O2",),
}
# Use an already-allowlisted image from the starter repository.
RUNNER = ("python:3.13-slim@sha256:"
          "d98cc29d3266e535dd9da1099688d9cb3ca4753813571e071763d17725460962")

# All timing is done INSIDE the container. Docker exec overhead is excluded.
RUN_CODE = r"""
import hashlib,json,os,resource,subprocess,sys,time
binary="/solver/"+sys.argv[1]
path="/out/"+sys.argv[2]
try: os.unlink(path)
except FileNotFoundError: pass
t=time.perf_counter_ns()
subprocess.run([binary],check=True,timeout=40,user=65534,group=65534)
seconds=(time.perf_counter_ns()-t)/1e9
with open(path,"rb") as f:
    sha=hashlib.file_digest(f,"sha256").hexdigest()
rss=resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss*1024
print(json.dumps({"seconds":seconds,"rss_bytes":rss,"sha256":sha}))
"""


def choices(track):
    grid = list(itertools.product(WORKERS, TUNE[track], STYLES))
    assert len(grid) == 100 and len(set(grid)) == 100
    return [(w, p, style) for w, p, style in grid]


def baseline():
    return 12, None, "base"


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def load_verified(track, args):
    manifest_path = args.manifest.resolve()
    if not manifest_path.is_file():
        raise RuntimeError(
            f"Missing {manifest_path}. Run: python3 scripts/fetch_data.py")
    manifest = json.loads(manifest_path.read_text())
    if manifest.get("seed") != 1:
        raise RuntimeError("Refusing unrecognised manifest seed")
    entry = manifest["tracks"][track]
    data = (args.data_root / track / "full-seed1").resolve()
    for filename in INPUTS[track]:
        path = data / filename
        expected = entry["inputs_sha256"][filename]
        if not path.is_file() or digest(path) != expected:
            raise RuntimeError(f"Input absent or SHA-256 mismatch: {path}")
    output_sha = entry["expected_output_sha256"]
    if len(output_sha) != 1:
        raise RuntimeError("Expected exactly one output hash")
    return data, output_sha[0]


def proxy(seconds, peak_bytes):
    # T and M are the varying axes; source lines and dependencies are fixed
    # for all 100 macro/compiler combinations of this track.
    if seconds <= 0 or peak_bytes <= 0:
        raise ValueError("Nonpositive timing/memory")
    return -0.5 * math.log(seconds) - 0.2 * math.log(peak_bytes)


def build(track, key, work):
    workers, param, style = key
    param = BASELINE_PARAM[track] if param is None else param
    filename = f"track-{track}-w{workers}-p{param}-{style}"
    executable = work / filename
    if executable.exists():
        return executable
    source = ROOT / f"track-{track}" / "src" / "main.cpp"
    command = ["g++", "-std=c++20", "-O3", "-DNDEBUG", "-flto",
               "-march=native", "-pthread", "-static", "-s",
               f"-DSD_TUNE_WORKERS={workers}",
               f"-DSD_TUNE_PARAM={param}",
               *STYLES[style], "-o", str(executable), str(source)]
    subprocess.run(command, check=True, timeout=240)
    return executable


def docker(*args, capture=True):
    result = subprocess.run(["docker", *args], text=True,
                            stdout=subprocess.PIPE if capture else None,
                            stderr=subprocess.PIPE if capture else None)
    if result.returncode:
        raise RuntimeError("Docker failed: " + " ".join(args[:2]) +
                           "\n" + (result.stderr or result.stdout or ""))
    return (result.stdout or "").strip()


@contextmanager
def environment(track, data, work, engine):
    out = work / "output"
    out.mkdir(parents=True, exist_ok=True)
    out.chmod(0o777)
    if engine == "native":
        if not all((Path("/data") / name).is_file() and
                   digest(Path("/data") / name) == digest(data / name)
                   for name in INPUTS[track]):
            raise RuntimeError(
                "Native mode requires the verified track inputs at /data")
        yield lambda exe: native_run(exe, OUTPUT[track])
        return

    name = "speed-demon-tuning-" + track + "-" + uuid.uuid4().hex[:10]
    try:
        # Official data warmed on the host before the solver container starts,
        # so the file cache is not charged to this new solver cgroup.
        for filename in INPUTS[track]:
            with (data / filename).open("rb") as stream:
                while stream.read(2 * 1024 * 1024):
                    pass
        docker("run", "-d", "--name", name, "--network=none",
               "--read-only", "--memory=8g", "--memory-swap=8g",
               "--pids-limit=512", "--cpus=12",
               "--cap-drop=ALL", "--cap-add=SETUID", "--cap-add=SETGID",
               "--security-opt=no-new-privileges",
               "--tmpfs", "/tmp:rw,size=1g,exec",
               "-v", f"{data}:/data:ro",
               "-v", f"{work}:/solver:ro",
               "-v", f"{out}:/out:rw", RUNNER,
               "python3", "-c", "import time;time.sleep(86400)")

        def run(exe):
            result = docker(
                "exec", name, "python3", "-c", RUN_CODE,
                exe.name, OUTPUT[track])
            return json.loads(result)

        yield run
    finally:
        if engine == "docker":
            try:
                docker("rm", "-f", name)
            except Exception as exc:
                print("WARNING: container cleanup:", exc, file=sys.stderr)


def native_run(exe, filename):
    out = Path("/out") / filename
    out.parent.mkdir(parents=True, exist_ok=True)
    out.unlink(missing_ok=True)
    start = time.perf_counter_ns()
    subprocess.run([str(exe)], check=True, timeout=40)
    seconds = (time.perf_counter_ns() - start) / 1e9
    return {"seconds": seconds, "rss_bytes":
            resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss * 1024,
            "sha256": digest(out)}


def checked_run(exe, runner, expected):
    result = runner(exe)
    if result["sha256"] != expected:
        raise RuntimeError(
            f"INCORRECT OUTPUT for {exe.name}: {result['sha256']} != {expected}")
    return result


def measure(exe, runner, expected, count):
    return [checked_run(exe, runner, expected) for _ in range(count)]


def result_record(key, samples):
    sec = statistics.median(s["seconds"] for s in samples)
    rss = max(s["rss_bytes"] for s in samples)
    return {"workers": key[0],
            "param": key[1], "style": key[2],
            "seconds": sec, "rss_bytes": rss,
            "log_proxy": proxy(sec, rss), "runs": len(samples)}


def apply_winner(track, winner):
    dockerfile = ROOT / f"track-{track}" / "Dockerfile"
    content = dockerfile.read_text()
    substitutions = {
        "SD_TUNE_WORKERS": str(winner["workers"]),
        "SD_TUNE_PARAM": str(winner["param"]),
        "SD_TUNE_EXTRA_FLAGS": " ".join(STYLES[winner["style"]]),
    }
    for name, value in substitutions.items():
        # The ARG values are in our experimental Dockerfiles only.
        pattern = rf"^ARG {name}=.*$"
        replacement = f'ARG {name}="{value}"'
        content, count = re.subn(pattern, lambda _: replacement, content,
                                 flags=re.MULTILINE)
        if count != 1:
            raise RuntimeError(f"Missing unique Dockerfile ARG {name}")
    dockerfile.write_text(content)


def campaign(track, args, report):
    data, expected = load_verified(track, args)
    work = (args.work / track).resolve()
    work.mkdir(parents=True, exist_ok=True)
    report[track] = {"track": track, "verified_dataset": str(data),
                     "expected_sha256": expected, "trials_planned": 100,
                     "trials_executed": 0, "failures": [],
                     "finalists": [], "status": "running"}
    with environment(track, data, work, args.engine) as runner:
        base_key = (12, BASELINE_PARAM[track], "base")
        original = build(track, base_key, work)
        base_samples = measure(original, runner, expected, args.final_reps)
        first_base = result_record(base_key, base_samples)
        ranked = []
        for i, key in enumerate(choices(track), start=1):
            try:
                exe = build(track, key, work)
                sample = checked_run(exe, runner, expected)
                item = result_record(key, [sample])
                ranked.append(item)
                report[track]["trials_executed"] += 1
            except Exception as exc:
                report[track]["failures"].append(
                    {"iteration": i, "configuration": list(key),
                     "error": str(exc)})
            if i % 10 == 0:
                print(f"Track {track.upper()}: {i}/100 attempted, "
                      f"{len(ranked)} exact-output passes", flush=True)
                args.report.write_text(json.dumps(report, indent=2) + "\n")
        ranked.sort(key=lambda r: r["log_proxy"], reverse=True)
        finalists = ranked[:args.finalists]
        for item in finalists:
            key = item["workers"], item["param"], item["style"]
            try:
                extra = measure(build(track, key, work), runner, expected,
                                args.final_reps)
                item.update(result_record(key, extra))
            except Exception as exc:
                report[track]["failures"].append(
                    {"finalist": list(key), "error": str(exc)})
                item["log_proxy"] = float("-inf")
        finalists.sort(key=lambda r: r["log_proxy"], reverse=True)
        final_base = result_record(base_key,
            measure(original, runner, expected, args.final_reps))
        baseline_score = min(first_base["log_proxy"], final_base["log_proxy"])
        best = finalists[0] if finalists else None
        report[track].update({
            "baseline_before": first_base,
            "baseline_after": final_base,
            "finalists": finalists,
            "winner": best,
            "note": "Local runtime/RSS proxy only; NOT official score.",
            "status": "completed",
        })
        if best:
            # The tool must not promote a noise-sized or incorrect result.
            gain = 100 * (math.exp(best["log_proxy"] - baseline_score) - 1)
            report[track]["relative_K_gain_pct_proxy"] = gain
            if args.apply and gain > 1 and best["runs"] >= args.final_reps:
                apply_winner(track, best)
                report[track]["applied_to_dockerfile"] = True
            else:
                report[track]["applied_to_dockerfile"] = False
    args.report.write_text(json.dumps(report, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tracks", nargs="+", choices=list("abcd"),
                        default=list("abcd"))
    parser.add_argument("--engine", choices=("docker", "native"),
                        default="docker")
    parser.add_argument("--data-root", type=Path, default=ROOT / "data")
    parser.add_argument("--manifest", type=Path,
                        default=ROOT / "data" / "SHA256SUMS.json")
    parser.add_argument("--work", type=Path, default=ROOT / ".tuning" / "bin")
    parser.add_argument("--report", type=Path,
                        default=ROOT / ".tuning" / "report.json")
    parser.add_argument("--finalists", type=int, default=8)
    parser.add_argument("--final-reps", type=int, default=5)
    parser.add_argument("--apply", action="store_true")
    parser.add_argument("--plan", action="store_true",
                        help="Print the 100 unique build configurations and exit")
    args = parser.parse_args()
    if args.plan:
        for track in args.tracks:
            print(track.upper(), len(choices(track)), "distinct configurations")
            for w, p, style in choices(track):
                print(f"  workers={w} param={p} compiler={style}")
        return
    if not (1 <= args.finalists <= 100 and args.final_reps >= 3):
        parser.error("finalists must be 1-100 and final-reps >= 3")
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.work.mkdir(parents=True, exist_ok=True)
    report = {}
    try:
        for track in args.tracks:
            campaign(track, args, report)
    finally:
        args.report.write_text(json.dumps(report, indent=2) + "\n")
    print("Reports:", args.report)
    if args.apply:
        print("Applied only >=1% proxy winners to the experimental Dockerfiles.")
        print("You MUST rerun official Docker correctness and benchmark before push.")
    print("No official machine points are inferred from this local proxy.")


if __name__ == "__main__":
    main()
