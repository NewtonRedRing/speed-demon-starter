#!/usr/bin/env python3
"""Download the official full public seed-1 inputs and verify every SHA-256."""
import argparse
import hashlib
import json
from pathlib import Path
import tempfile
import urllib.request

BASE = "https://s3.amazonaws.com/raptors.dev/speeddemon.dev/2026/data"
INPUTS = {
    "a": ("requests.log",),
    "b": ("docs.txt", "queries.txt"),
    "c": ("records.csv", "regions.csv"),
    "d": ("graph.txt", "queries.txt"),
}
OUTPUTS = {"a": "summary.txt", "b": "results.txt", "c": "aggregates.txt", "d": "distances.txt"}
ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def load_manifest():
    request = urllib.request.Request(BASE + "/SHA256SUMS.json",
                                     headers={"User-Agent": "NewtonRedHeat-dataset-check/1.0"})
    with urllib.request.urlopen(request, timeout=30) as response:
        manifest = json.load(response)
    if manifest.get("seed") != 1 or set(manifest.get("tracks", {})) != set(INPUTS):
        raise RuntimeError("unexpected official dataset manifest")
    for track, names in INPUTS.items():
        entry = manifest["tracks"][track]
        checksums = entry["inputs_sha256"]
        outputs = entry["expected_output_sha256"]
        if set(checksums) != set(names) or entry["output"] != OUTPUTS[track]:
            raise RuntimeError(f"unexpected official filenames for track {track}")
        if not isinstance(outputs, list) or len(outputs) != 1:
            raise RuntimeError(f"expected one official output hash for track {track}")
        for expected in [*checksums.values(), *outputs]:
            if not isinstance(expected, str) or len(expected) != 64 or any(
                c not in "0123456789abcdef" for c in expected
            ):
                raise RuntimeError(f"invalid official SHA-256 for track {track}")
    return manifest


def verify_inputs(track, data, manifest):
    for name, expected in manifest["tracks"][track]["inputs_sha256"].items():
        path = data / name
        if not path.is_file() or digest(path) != expected:
            raise RuntimeError(f"official input missing or SHA-256 mismatch: {path}")


def fetch(path, url, expected, force=False):
    if path.exists() and not force and digest(path) == expected:
        print(f"verified {path.name}", flush=True)
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    print(f"downloading {path.name}...", flush=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as output:
            temporary = Path(output.name)
            actual = hashlib.sha256()
            request = urllib.request.Request(url, headers={"User-Agent": "NewtonRedHeat-dataset-check/1.0"})
            with urllib.request.urlopen(request, timeout=60) as response:
                while chunk := response.read1(1024 * 1024):
                    output.write(chunk)
                    actual.update(chunk)
        if actual.hexdigest() != expected:
            raise RuntimeError(f"SHA-256 mismatch downloading {path.name}: {actual.hexdigest()}")
        temporary.replace(path)
        print(f"downloaded and verified {path.name}: {path.stat().st_size:,} bytes", flush=True)
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--force", action="store_true", help="download even when local hashes already match")
    args = parser.parse_args()
    manifest = load_manifest()
    for track, names in INPUTS.items():
        checksums = manifest["tracks"][track]["inputs_sha256"]
        for name in names:
            expected = checksums[name]
            fetch(ROOT / "data" / track / "full-seed1" / name,
                  f"{BASE}/track-{track}/full-seed1/{name}", expected, args.force)
    (ROOT / "data" / "SHA256SUMS.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
