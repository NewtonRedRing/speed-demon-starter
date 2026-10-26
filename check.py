#!/usr/bin/env python3
"""Build one track the way the Speed Demon board does, run it, and check its output.

    python3 check.py a --data data/a [--expected <sha256>] [--skip-build]

The build is offline (--network=none), like the board's. The run uses the board's
sandbox settings: no network, a read-only filesystem, an unprivileged user, 8 GB of
memory, 512 processes and 1 GB of /tmp. The time printed includes container start-up;
the board times your process alone, on its own machine.
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

OUTPUT = {"a": "summary.txt", "b": "results.txt", "c": "aggregates.txt", "d": "distances.txt"}
ALLOWLIST_URL = "https://speeddemon.dev/allowlist.json"
IMAGE_MAX_BYTES = 2 * 1024 ** 3
STAGING_IMAGE = "python:3.13-slim@sha256:d98cc29d3266e535dd9da1099688d9cb3ca4753813571e071763d17725460962"
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

        cmd = ["docker", "run", "--name", runner, "--network=none", "--read-only",
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

    cmd = ["docker", "run", "--rm", "--network=none", "--read-only", "--tmpfs", "/tmp:rw,size=1g,exec",
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


def main():
    ap = argparse.ArgumentParser(description="Build and run one Speed Demon track the way the board does.")
    ap.add_argument("track", choices=sorted(OUTPUT))
    ap.add_argument("--data", required=True, help="the folder holding the track's input files")
    ap.add_argument("--expected", help="the sha256 the output must match")
    ap.add_argument("--skip-build", action="store_true", help="run the image from the last build")
    args = ap.parse_args()

    if shutil.which("docker") is None:
        fail("docker is not on PATH")
    dockerfile = os.path.join(ROOT, f"track-{args.track}", "Dockerfile")
    if not os.path.isfile(dockerfile):
        fail(f"track-{args.track}/Dockerfile is missing")
    data = os.path.abspath(args.data)
    if not os.path.isdir(data):
        fail(f"{data} is not a folder")

    tag = f"speeddemon-check/track-{args.track}"
    if not args.skip_build:
        digests = allowlisted_digests()
        if digests is not None:
            with open(dockerfile, encoding="utf-8") as f:
                problems = from_problems(f.read(), digests)
            if problems:
                fail("; ".join(problems) + " (the board refuses this)")
            print("ok    every FROM is allowlisted")
        build(args.track, tag)

    out, took = run(args.track, tag, data)
    name = OUTPUT[args.track]
    found = sorted(os.listdir(out))
    if found != [name]:
        fail(f"/out must hold exactly {name}, found {found}")
    digest = sha256(os.path.join(out, name))
    print(f"ok    {name} sha256 {digest}")
    print(f"time  {took:.2f} s, including container start-up; the board's number will differ")
    if args.expected:
        if digest != args.expected.strip().lower():
            fail(f"the output does not match the expected sha256 {args.expected}")
        print("PASS  the output matches the expected sha256")
    else:
        print("note  no --expected given. If this folder is unchanged, that sha256 is the right answer; check against it:")
        print(f"      python3 check.py {args.track} --data {args.data} --expected {digest}")


if __name__ == "__main__":
    main()
