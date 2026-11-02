#!/usr/bin/env python3
"""Download and SHA-256 verify published, public synthetic input files.

Files are stored outside version control. A truncated or mismatching download is
deleted, and successful existing files are reused.
"""
import argparse
import hashlib
from pathlib import Path
import urllib.request

BASE = "https://s3.amazonaws.com/raptors.dev/speeddemon.dev/2026/data"
FILES = {
  "log_ingest": {
    "track": "a",
    "hashes": {
      "requests.log": "7ec80ba45bceef51ac7f79d3a14521fc6187b2dcb87b5e5542d08063fe5d962a"
    }
  },
  "text_search": {
    "track": "b",
    "hashes": {
      "docs.txt": "6cd461670d6c39f9c5211162577532d3648edeab12340c71cc7d2ab5f18e66c8",
      "queries.txt": "77ef7ffc59afde051006b02a2ce8e4c090fbbfbf101acd6f862d0f8e298d39f9"
    }
  },
  "csv_aggregate": {
    "track": "c",
    "hashes": {
      "records.csv": "36166f4ac44101a22ebf25b4f949436a681a2698cb0f2ae1f7f6853010259f48",
      "regions.csv": "414c57c4f3ec968f9bce28c59eebfdf57a9425322b468db7ee964c18c4852fe1"
    }
  },
  "shortest_path": {
    "track": "d",
    "hashes": {
      "graph.txt": "fe6471f2712d10b23c2ec58a4cbc7db1ea3e742daf6b6db62829e74d6c4a26c7",
      "queries.txt": "160f1ef48378dcb567a8d9ddd2676ee777b044a5ede3ad4c7b42e812aa4b3a6f"
    }
  }
}

def download(url, target, checksum):
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.exists() and digest(target) == checksum:
        print("verified (cached):", target)
        return
    temporary = target.with_suffix(target.suffix + ".partial")
    try:
        with urllib.request.urlopen(url, timeout=180) as response, temporary.open("wb") as out:
            while chunk := response.read(1024 * 1024):
                out.write(chunk)
        if digest(temporary) != checksum:
            raise ValueError("SHA-256 mismatch: " + str(target))
        temporary.replace(target)
        print("verified:", target)
    finally:
        temporary.unlink(missing_ok=True)

def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as handle:
        while chunk := handle.read(1024 * 1024):
            h.update(chunk)
    return h.hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", choices=[*FILES, "all"], default="all")
    parser.add_argument("--dest", type=Path, default=Path("data"))
    args = parser.parse_args()
    for engine, details in FILES.items():
        if args.engine not in (engine, "all"):
            continue
        for name, sha in details["hashes"].items():
            url = f"{BASE}/track-{details['track']}/full-seed1/{name}"
            download(url, args.dest / engine / name, sha)

if __name__ == "__main__":
    main()
