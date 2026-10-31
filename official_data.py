#!/usr/bin/env python3
"""Download and verify the ORIGINAL Speed Demon 2026 full public seed-1 data.

The seven files and all SHA-256 hashes below are pinned from the organizer's
https://s3.amazonaws.com/raptors.dev/speeddemon.dev/2026/data/SHA256SUMS.json
Do not replace them with output from the included synthetic PGO generators.

Usage:
    python3 official_data.py download             # all four tracks, ~790.5 MB
    python3 official_data.py download a c         # chosen tracks
    python3 official_data.py verify               # verify all cached inputs
    python3 check.py a                            # always run original A input
    python3 check.py all                          # all original full datasets

The competition's final holdout seed is unpublished and cannot be downloaded.
"""

import argparse
import hashlib
import os
from pathlib import Path
import sys
import urllib.error
import urllib.request

BASE = "https://s3.amazonaws.com/raptors.dev/speeddemon.dev/2026/data"
ROOT = Path(__file__).resolve().parent
CACHE = ROOT / "data"

# Immutable published manifest: seed=1, scale=full, release 2026-10-30.
TRACKS = {
    "a": {
        "inputs": {
            "requests.log": "7ec80ba45bceef51ac7f79d3a14521fc6187b2dcb87b5e5542d08063fe5d962a",
        },
        "output": "summary.txt",
        "sha256": "495ca09682b9673ef836a506496177680bb4a3af54255174befddb1f83481cd3",
    },
    "b": {
        "inputs": {
            "docs.txt": "6cd461670d6c39f9c5211162577532d3648edeab12340c71cc7d2ab5f18e66c8",
            "queries.txt": "77ef7ffc59afde051006b02a2ce8e4c090fbbfbf101acd6f862d0f8e298d39f9",
        },
        "output": "results.txt",
        "sha256": "f86973c210228141bcb6f1f1b10188ad2a9b65f71e7cd7cf4590b38de4e80927",
    },
    "c": {
        "inputs": {
            "records.csv": "36166f4ac44101a22ebf25b4f949436a681a2698cb0f2ae1f7f6853010259f48",
            "regions.csv": "414c57c4f3ec968f9bce28c59eebfdf57a9425322b468db7ee964c18c4852fe1",
        },
        "output": "aggregates.txt",
        "sha256": "d8088b1cd42ba1338b1345b107b55db7b96ef1f5066f3ff64fd26a8912a0f536",
    },
    "d": {
        "inputs": {
            "graph.txt": "fe6471f2712d10b23c2ec58a4cbc7db1ea3e742daf6b6db62829e74d6c4a26c7",
            "queries.txt": "160f1ef48378dcb567a8d9ddd2676ee777b044a5ede3ad4c7b42e812aa4b3a6f",
        },
        "output": "distances.txt",
        "sha256": "a23dbdf0a3e49cc8901310a9ee1dc6a9a569a85297b6da75c2a7002cb2ed277e",
    },
}


class DatasetError(RuntimeError):
    """Missing, corrupted, or nonofficial public inputs."""


def sha256_file(filename):
    h = hashlib.sha256()
    with open(filename, "rb") as stream:
        for chunk in iter(lambda: stream.read(4 * 1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def dataset_path(track):
    return CACHE / track


def verify(track, directory=None):
    """Fail closed if any input differs by even one byte from the official file."""
    if track not in TRACKS:
        raise DatasetError(f"unknown track: {track}")
    directory = Path(directory) if directory is not None else dataset_path(track)
    inputs = TRACKS[track]["inputs"]
    if not directory.is_dir():
        raise DatasetError(f"missing official Track {track.upper()} dataset: {directory}")
    found = {p.name for p in directory.iterdir()}
    if found != set(inputs):
        raise DatasetError(
            f"Track {track.upper()} /data must contain exactly {sorted(inputs)}; "
            f"found {sorted(found)} in {directory}"
        )
    for name, expected in inputs.items():
        path = directory / name
        if not path.is_file() or path.is_symlink():
            raise DatasetError(f"not a regular dataset file: {path}")
        observed = sha256_file(path)
        if observed != expected:
            raise DatasetError(
                f"NONOFFICIAL/CORRUPT INPUT {path}: expected SHA-256 {expected}, got {observed}. "
                "Remove or move the bad file, then download the official copy."
            )
        print(f"ok    official seed-1/full Track {track.upper()} {name} sha256={observed}")
    return directory.resolve()


def download(track, directory=None):
    """Get the exact published files; never silently overwrite a different dataset."""
    if track not in TRACKS:
        raise DatasetError(f"unknown track: {track}")
    directory = Path(directory) if directory is not None else dataset_path(track)
    directory.mkdir(parents=True, exist_ok=True)
    expected_names = TRACKS[track]["inputs"]
    extra = {p.name for p in directory.iterdir()} - set(expected_names)
    if extra:
        raise DatasetError(f"{directory} contains files not in the official dataset: {sorted(extra)}")
    for name, digest in expected_names.items():
        path = directory / name
        if path.exists():
            observed = sha256_file(path)
            if observed != digest:
                raise DatasetError(
                    f"Existing {path} is NOT the official input (sha256={observed}). "
                    "Move it away before downloading the original."
                )
            print(f"cached official {track.upper()} {name}")
            continue
        url = f"{BASE}/track-{track}/full-seed1/{name}"
        temp = path.with_name(name + ".official-download")
        if temp.exists():
            temp.unlink()  # An incomplete previous download is never trusted.
        print(f"download {url}", flush=True)
        h = hashlib.sha256()
        try:
            request = urllib.request.Request(url, headers={"User-Agent": "speed-demon-official-data/1.0"})
            with urllib.request.urlopen(request, timeout=90) as response, open(temp, "xb") as output:
                while True:
                    block = response.read(4 * 1024 * 1024)
                    if not block:
                        break
                    output.write(block)
                    h.update(block)
            observed = h.hexdigest()
            if observed != digest:
                raise DatasetError(
                    f"download verification FAILED {name}: expected {digest}, got {observed}"
                )
            os.replace(temp, path)
            print(f"ok    official {track.upper()} {name} sha256={observed}")
        finally:
            if temp.exists():
                temp.unlink()
    return verify(track, directory)


def ensure(track, directory=None):
    """Auto-download only missing default-cache files, then verify all inputs.

    A custom --data directory is never downloaded into or changed. It MUST
    already contain byte-for-byte copies of the original published files.
    """
    if directory is None:
        return download(track)
    return verify(track, directory)


def main(argv=None):
    parser = argparse.ArgumentParser(description="Only official Speed Demon full public seed-1 inputs")
    parser.add_argument("command", choices=["download", "verify"])
    parser.add_argument("tracks", nargs="*", choices=list(TRACKS), metavar="TRACK",
                        help="a, b, c, d (default: all)")
    args = parser.parse_args(argv)
    try:
        for track in (args.tracks or list(TRACKS)):
            if args.command == "download":
                download(track)
            else:
                verify(track)
    except (DatasetError, OSError, urllib.error.URLError) as error:
        parser.exit(1, f"FAIL  {error}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
