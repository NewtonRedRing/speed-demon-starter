#!/usr/bin/env python3
"""Verify exact outputs and measure the solution process, not Docker startup."""
import argparse
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys
import uuid
from fetch_data import BASE, load_manifest, verify_inputs

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import check

MEASURE = """
import hashlib,json,os,resource,subprocess,time
os.chmod('/out',0o777)
samples=[]
for i in range(int(os.environ['RUNS'])):
 for name in os.listdir('/out'): os.unlink('/out/'+name)
 start=time.perf_counter()
 subprocess.run(['/solver/solution'],check=True,user=65534,group=65534)
 elapsed=time.perf_counter()-start
 names=sorted(os.listdir('/out'))
 expected=os.environ['OUTPUT']
 if names != [expected]: raise RuntimeError('unexpected output files: '+repr(names))
 with open('/out/'+expected,'rb') as f: digest=hashlib.file_digest(f,'sha256').hexdigest()
 if digest != os.environ['EXPECTED']: raise RuntimeError('incorrect output: '+digest)
 samples.append(elapsed)
print(json.dumps({'seconds':samples,'peak_rss_bytes':resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss*1024,'sha256':digest}))
"""


def docker(*args):
    result = subprocess.run(
        ["docker", *map(str, args)], capture_output=True, text=True
    )
    if result.returncode:
        raise RuntimeError(f"docker {args[0]} failed: {result.stderr.strip() or result.stdout.strip()}")
    if result.stderr.strip():
        print(result.stderr.strip(), file=sys.stderr)
    return result.stdout.strip()


def measure(track, data, expected, runs, tag):
    prefix = f"speed-demon-bench-{uuid.uuid4().hex[:12]}"
    volumes = [prefix + suffix for suffix in ("-data", "-out", "-solver")]
    containers = []
    try:
        for volume in volumes:
            docker("volume", "create", volume)
        solver = prefix + "-solution"
        docker("create", "--name", solver, "-v", f"{volumes[2]}:/app", tag)
        containers.append(solver)
        stager = prefix + "-stage"
        docker("create", "--name", stager, "--network=none",
               "-v", f"{volumes[0]}:/data", check.STAGING_IMAGE,
               "python3", "-c", "import time;time.sleep(3600)")
        containers.append(stager)
        docker("start", stager)
        docker("cp", str(data) + os.sep + ".", f"{stager}:/data/")
        docker("exec", stager, "python3", "-c",
               "import os\nfor root,dirs,files in os.walk('/data'):\n"
               " os.chmod(root,0o755)\n for f in files: os.chmod(os.path.join(root,f),0o644)")
        runner = prefix + "-run"
        containers.append(runner)
        result = docker(
            "run", "--name", runner, "--network=none", "--read-only",
            "--memory=8g", "--memory-swap=8g", "--pids-limit=512",
            "--cap-drop=ALL", "--security-opt=no-new-privileges",
            "--cap-add=SETUID", "--cap-add=SETGID", "--cpus=12",
            "--tmpfs", "/tmp:rw,size=1g,exec",
            "-v", f"{volumes[0]}:/data:ro", "-v", f"{volumes[1]}:/out",
            "-v", f"{volumes[2]}:/solver:ro",
            "-e", f"TRACK={track}", "-e", f"RUNS={runs}",
            "-e", f"OUTPUT={check.OUTPUT[track]}", "-e", f"EXPECTED={expected}",
            check.STAGING_IMAGE, "python3", "-c", MEASURE,
        )
        return json.loads(result)
    finally:
        check.cleanup_docker(containers, volumes)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tracks", nargs="+", choices=list("abcd"))
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--tag-prefix", default="speeddemon-check/track-")
    parser.add_argument("--build", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.runs < 1:
        parser.error("--runs must be positive")
    manifest = load_manifest()
    report = {}
    for track in args.tracks:
        tag = args.tag_prefix + track
        if args.build:
            check.build(track, tag)
        data = Path(check.ROOT) / "data" / track / "full-seed1"
        verify_inputs(track, data, manifest)
        expected = manifest["tracks"][track]["expected_output_sha256"][0]
        result = measure(track, data, expected, args.runs, tag)
        result["manifest_url"] = BASE + "/SHA256SUMS.json"
        result["inputs_sha256"] = manifest["tracks"][track]["inputs_sha256"]
        result["median_seconds"] = statistics.median(result["seconds"])
        report[track] = result
        print(f"{track}: {result['median_seconds']:.6f} s, "
              f"{result['peak_rss_bytes'] / 1048576:.2f} MiB, exact hash PASS",
              flush=True)
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
