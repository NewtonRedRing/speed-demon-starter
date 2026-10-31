#!/usr/bin/env python3
"""Download the official Speed Demon seed-1 *full* datasets, verify SHA256,
and optionally build/run the current branch with the repository's check.py.

Usage:
    python3 scripts/public_full_benchmark.py --tracks a b c d --download-only
    python3 scripts/public_full_benchmark.py --tracks d
    python3 scripts/public_full_benchmark.py --tracks a b c d

Python 3.13+, Docker for actual checks. Data is never committed or modified.
"""
import argparse
import hashlib
import json
import pathlib
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

BASE = "https://s3.amazonaws.com/raptors.dev/speeddemon.dev/2026/data"
FILES = {
    "a": ["requests.log"],
    "b": ["docs.txt", "queries.txt"],
    "c": ["records.csv", "regions.csv"],
    "d": ["graph.txt", "queries.txt"],
}
ROOT = pathlib.Path(__file__).resolve().parents[1]


def download(url: str, destination: pathlib.Path, sha256: str) -> bool:
    """Stream to disk with an atomic rename; never trust an unchecked download."""
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists() and digest(destination) == sha256:
        print(f"verified cached: {destination}", flush=True)
        return False
    with tempfile.NamedTemporaryFile(prefix=destination.name + ".", suffix=".part",
                                     dir=destination.parent, delete=False) as output:
        temporary = pathlib.Path(output.name)
    start = time.monotonic()
    try:
        request = urllib.request.Request(url, headers={"User-Agent": "SpeedDemonExactBenchmark/1.0"})
        sha = hashlib.sha256()
        with urllib.request.urlopen(request, timeout=120) as response, temporary.open("wb") as output:
            while chunk := response.read(2 * 1024 * 1024):
                output.write(chunk)
                sha.update(chunk)
        actual = sha.hexdigest()
        if actual != sha256:
            raise ValueError(f"Hash mismatch for {destination}: {actual} != {sha256}")
        temporary.replace(destination)
        print(f"verified: {destination} ({time.monotonic()-start:.1f}s transfer)", flush=True)
        return True
    finally:
        temporary.unlink(missing_ok=True)


def digest(path: pathlib.Path) -> str:
    sha = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(2 * 1024 * 1024), b""):
            sha.update(chunk)
    return sha.hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--tracks", nargs="+", choices=sorted(FILES), default=list(FILES))
    parser.add_argument("--download-only", action="store_true")
    parser.add_argument("--data-root", type=pathlib.Path, default=ROOT / ".data" / "official-full")
    args = parser.parse_args()

    metadata_url = f"{BASE}/SHA256SUMS.json"
    request = urllib.request.Request(metadata_url, headers={"User-Agent": "SpeedDemonExactBenchmark/1.0"})
    with urllib.request.urlopen(request, timeout=30) as stream:
        metadata = json.load(stream)
    if metadata.get("seed") != 1:
        raise ValueError("Unexpected official manifest seed: refusing download")

    for track in args.tracks:
        part = metadata["tracks"][track]
        directory = args.data_root.resolve() / track
        for filename in FILES[track]:
            download(f"{BASE}/track-{track}/full-seed1/{filename}",
                     directory / filename, part["inputs_sha256"][filename])
        if args.download_only:
            continue
        print(f"BUILD + EXACT VALIDATION: Track {track}", flush=True)
        expected = part["expected_output_sha256"][0]
        cmd = [sys.executable, str(ROOT / "check.py"), track,
               "--data", str(directory), "--expected", expected]
        subprocess.run(cmd, cwd=ROOT, check=True)

    print("PASS: selected official full files verified" +
          ("; outputs validated" if not args.download_only else ""), flush=True)


if __name__ == "__main__":
    main()
