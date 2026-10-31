#!/usr/bin/env python3
"""Check ONLY the original, full-scale official public seed-1 datasets.

    python3 check.py a                 # downloads official Track A if missing
    python3 check.py all               # downloads and tests all four tracks
    python3 check.py b --skip-build    # reuse the previous official image

SHA-256 is enforced for EVERY input and EVERY output using the organizers'
released manifest; synthetic, sample and generated datasets are rejected.
The build uses the submission's pinned, offline Dockerfile. The runtime uses
the official sandbox flags, including 12-core CPU and NUMA pinning by default.
Local measurements are not official performance scores: the scoring CPU,
host, cache accounting and final hidden holdout cannot be reproduced locally.
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
import urllib.request
import uuid

import official_data

OUTPUT = {"a": "summary.txt", "b": "results.txt", "c": "aggregates.txt", "d": "distances.txt"}
ALLOWLIST_URL = "https://speeddemon.dev/allowlist.json"
IMAGE_MAX_BYTES = 2 * 1024 ** 3
STAGING_IMAGE = "python:3.13-slim@sha256:d98cc29d3266e535dd9da1099688d9cb3ca4753813571e071763d17725460962"
# Defaults match the official scoring harness. On a local 4-core CI/WSL
# machine you can override only CPU pinning, never the dataset or output hash.
CPUSET_CPUS = os.environ.get("SPEED_DEMON_CPUSET_CPUS", "0-11")
CPUSET_MEMS = os.environ.get("SPEED_DEMON_CPUSET_MEMS", "0")
ROOT = os.path.dirname(os.path.abspath(__file__))


def fail(message):
    print(f"FAIL  {message}")
    sys.exit(1)


def allowlisted_digests():
    # The site's CDN refuses urllib's default User-Agent, so name the client.
    req = urllib.request.Request(ALLOWLIST_URL, headers={"User-Agent": "speed-demon-starter-check/1.0"})
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            return {e["digest"] for e in json.load(resp)["images"]}
    except Exception as exc:  # offline or unreachable: the board still checks
        print(f"note  could not fetch {ALLOWLIST_URL} ({type(exc).__name__}); skipping the FROM check")
        return None


def from_problems(dockerfile, digests):
    """FROM lines the board would refuse: not an earlier stage, not pinned, or not allowlisted."""
    stages, problems = set(), []
    for raw in dockerfile.splitlines():
        words = [w for w in raw.split() if not w.startswith("--")]
        if len(words) < 2 or words[0].upper() != "FROM":
            continue
        image = words[1]
        if image.lower() not in stages:
            if "@sha256:" not in image:
                problems.append(f"FROM {image} is not pinned by digest")
            elif image.split("@", 1)[1] not in digests:
                problems.append(f"FROM {image} is not in the allowlist")
        if len(words) >= 4 and words[2].upper() == "AS":
            stages.add(words[3].lower())
    return problems


def build(track, tag):
    print(f"build docker build --network=none -t {tag} track-{track}")
    if subprocess.run(["docker", "build", "--network=none", "-t", tag, os.path.join(ROOT, f"track-{track}")]).returncode:
        fail("the build failed; it has to work offline, so commit your dependencies")
    size = int(subprocess.run(["docker", "image", "inspect", "-f", "{{.Size}}", tag],
                              capture_output=True, text=True, check=True).stdout.strip())
    if size > IMAGE_MAX_BYTES:
        fail(f"the image is {size / 1e9:.2f} GB; the board's cap is 2 GB")
    print(f"ok    image is {size / 1e6:.0f} MB")


def checked_docker(arguments, action):
    result = subprocess.run(["docker", *arguments], capture_output=True, text=True)
    if result.returncode:
        detail = result.stderr.strip() or result.stdout.strip() or f"exit code {result.returncode}"
        fail(f"{action}: {detail}")
    return result


def cleanup_docker(containers, volumes):
    for container in reversed(containers):
        result = subprocess.run(["docker", "rm", "--force", container], capture_output=True, text=True)
        detail = (result.stderr or result.stdout).strip()
        if result.returncode and "no such container" not in detail.lower():
            print(f"warning could not remove Docker container {container}: {detail}", file=sys.stderr)
    for volume in reversed(volumes):
        result = subprocess.run(["docker", "volume", "rm", "--force", volume], capture_output=True, text=True)
        detail = (result.stderr or result.stdout).strip()
        if result.returncode and "no such volume" not in detail.lower():
            print(f"warning could not remove Docker volume {volume}: {detail}", file=sys.stderr)


def run_with_named_volumes(track, tag, data, out):
    suffix = uuid.uuid4().hex[:12]
    data_volume = f"speed-demon-check-{track}-{suffix}-data"
    out_volume = f"speed-demon-check-{track}-{suffix}-out"
    stager = f"speed-demon-check-{track}-{suffix}-stage"
    runner = f"speed-demon-check-{track}-{suffix}-run"
    containers, volumes = [], []
    try:
        for volume in (data_volume, out_volume):
            checked_docker(["volume", "create", volume], f"could not create temporary Docker volume {volume}")
            volumes.append(volume)

        checked_docker(
            ["create", "--name", stager, "--network=none",
             "-v", f"{data_volume}:/data", "-v", f"{out_volume}:/out",
             STAGING_IMAGE, "python3", "-c", "import time; time.sleep(3600)"],
            "could not create the temporary data-transfer container",
        )
        containers.append(stager)
        checked_docker(["start", stager], "could not start the temporary data-transfer container")
        checked_docker(
            ["cp", os.path.join(data, "."), f"{stager}:/data/"],
            "could not copy input data into the temporary Docker volume",
        )
        permissions = (
            "import os\n"
            "for root, dirs, files in os.walk('/data'):\n"
            " os.chmod(root, 0o755)\n"
            " for name in dirs:\n"
            "  os.chmod(os.path.join(root, name), 0o755)\n"
            " for name in files:\n"
            "  path = os.path.join(root, name)\n"
            "  if not os.path.islink(path): os.chmod(path, 0o644)\n"
            "os.chmod('/out', 0o777)\n"
        )
        checked_docker(
            ["exec", stager, "python3", "-c", permissions],
            "could not set permissions on the temporary Docker volumes",
        )
        checked_docker(["rm", "--force", stager], "could not remove the data-transfer container")
        containers.remove(stager)

        cmd = ["docker", "run", "--name", runner, f"--cpuset-cpus={CPUSET_CPUS}", f"--cpuset-mems={CPUSET_MEMS}",
               "--network=none", "--read-only",
               "--tmpfs", "/tmp:rw,size=1g,exec", "--memory=8g", "--memory-swap=8g",
               "--pids-limit=512", "--cap-drop=ALL", "--security-opt=no-new-privileges",
               "--user", "65534:65534", "--ulimit", "nofile=8192:8192",
               "-e", f"TRACK={track}", "-v", f"{data_volume}:/data:ro",
               "-v", f"{out_volume}:/out:rw", tag]
        containers.append(runner)
        started = time.monotonic()
        code = subprocess.run(cmd).returncode
        took = time.monotonic() - started
        if code:
            fail(f"the run exited {code} after {took:.2f} s")

        output = OUTPUT[track]
        checked_docker(
            ["cp", f"{runner}:/out/{output}", os.path.join(out, output)],
            "could not copy the container output back to the host",
        )
        return out, took
    finally:
        cleanup_docker(containers, volumes)


def run(track, tag, data):
    out = os.path.join(ROOT, ".out", f"track-{track}")
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    os.chmod(out, 0o777)  # the run is an unprivileged user, as on the board
    if os.name == "nt":
        return run_with_named_volumes(track, tag, data, out)

    cmd = ["docker", "run", "--rm", f"--cpuset-cpus={CPUSET_CPUS}", f"--cpuset-mems={CPUSET_MEMS}",
           "--network=none", "--read-only", "--tmpfs", "/tmp:rw,size=1g,exec",
           "--memory=8g", "--memory-swap=8g", "--pids-limit=512", "--cap-drop=ALL",
           "--security-opt=no-new-privileges", "--user", "65534:65534", "--ulimit", "nofile=8192:8192",
           "-e", f"TRACK={track}", "-v", f"{data}:/data:ro", "-v", f"{out}:/out:rw", tag]
    started = time.monotonic()
    code = subprocess.run(cmd).returncode
    took = time.monotonic() - started
    if code:
        fail(f"the run exited {code} after {took:.2f} s")
    return out, took


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def check_track(track, path, skip_build, allowed):
    """Build and run against the original verified full seed-1 input only."""
    print(f"\n== Track {track.upper()} — OFFICIAL full seed-1 ==")
    # Verify the entire input before the build; checksum reads also warm it
    # into the OS page cache, approximating the organizer's vmtouch step.
    data = official_data.ensure(track, path)
    tag = f"speeddemon-check/track-{track}"
    if not skip_build:
        if allowed is not None:
            with open(os.path.join(ROOT, f"track-{track}", "Dockerfile"), encoding="utf-8") as f:
                problems = from_problems(f.read(), allowed)
            if problems:
                fail("; ".join(problems) + " (the official harness refuses this)")
            print("ok    every Docker FROM is on the official allowlist")
        build(track, tag)

    out, took = run(track, tag, str(data))
    expected_name = official_data.TRACKS[track]["output"]
    files = sorted(os.listdir(out))
    if files != [expected_name]:
        fail(f"/out must contain exactly {expected_name}; got {files}")
    digest = sha256(os.path.join(out, expected_name))
    expected = official_data.TRACKS[track]["sha256"]
    print(f"output {expected_name} SHA-256 {digest}")
    if digest != expected:
        fail(
            f"Track {track.upper()} FAILED: official expected SHA-256 {expected}, "
            f"but executable produced {digest}"
        )
    print(f"PASS  Track {track.upper()} official full-data correctness")
    print(f"time  {took:.3f} s (includes Docker startup; NOT the official scoring time)")


def main():
    ap = argparse.ArgumentParser(
        description="Always use and verify the original full public seed-1 Speed Demon inputs."
    )
    ap.add_argument("track", choices=[*sorted(OUTPUT), "all"])
    ap.add_argument("--data", help="alternative data directory ONLY if every official input SHA matches")
    ap.add_argument("--expected", help="compatibility flag: must match the PUBLISHED official output SHA")
    ap.add_argument("--skip-build", action="store_true", help="reuse the last locally built image")
    args = ap.parse_args()

    if args.track == "all" and (args.data or args.expected):
        ap.error("--data and --expected are track-specific; omit them when using 'all'")
    if shutil.which("docker") is None:
        fail("docker is not on PATH (start Docker Desktop and enable WSL integration)")

    tracks = sorted(OUTPUT) if args.track == "all" else [args.track]
    if (CPUSET_CPUS, CPUSET_MEMS) != ("0-11", "0"):
        print(
            f"NOTE  local CPU pinning differs from the official runner: "
            f"cpus={CPUSET_CPUS} mems={CPUSET_MEMS}. "
            "Dataset/output verification is still exact; runtime is not comparable."
        )
    if args.expected:
        official = official_data.TRACKS[args.track]["sha256"]
        if args.expected.strip().lower() != official:
            fail(
                f"--expected must equal the OFFICIAL {args.track.upper()} output SHA-256 "
                f"{official}; synthetic/alternate expected hashes are forbidden"
            )

    allowed = None if args.skip_build else allowlisted_digests()
    for track in tracks:
        try:
            check_track(track, args.data, args.skip_build, allowed)
        except (official_data.DatasetError, OSError, urllib.error.URLError) as e:
            fail(str(e))
    print("\nPASS  all requested tracks match the original published full-scale output hashes")


if __name__ == "__main__":
    main()
