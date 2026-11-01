#!/usr/bin/env python3
"""Generate a LOCAL-ONLY instrumented copy of Track D to time its five stages.

Usage:
  python3 tools/profile_d_phases.py --repo . --out /tmp/sd-d-phases
  docker build --network=none --target build -t sd-d-phases /tmp/sd-d-phases

Run the resulting executable under an authorised local clone of the official
harness (or a local container with /data and /out) and inspect its stderr.
Do not use this instrumented copy as a scored competition submission.
"""
from __future__ import annotations

import argparse
import pathlib
import shutil

# Insert timers before these stable milestones in the winning Track D source.
MARKERS = [
    ("    // 2. Build each part's adjacency on its own.", "input_and_filing"),
    ("    // 3. Locality order: compact BFS blobs.", "forward_csr"),
    ("    // The reverse graph in the same format:", "renumber_and_remap"),
    ("    // 4. Queries sorted so neighbouring searches share cached pages.", "reverse_csr"),
    ("    // INF prints as -1 (u64::MAX as i64); every real distance is below 2^63.", "query_search"),
]

def instrument(source: str) -> str:
    begin = "fn main() {"
    if source.count(begin) != 1:
        raise ValueError("Expected one main function; update markers for new source")
    source = source.replace(begin, begin + "\n    let mut __sd_phase = std::time::Instant::now();", 1)
    for marker, label in MARKERS:
        if source.count(marker) != 1:
            raise ValueError(f"Expected one phase marker for {label}; source has changed")
        code = (
            "    let __sd_now = std::time::Instant::now();\n"
            f'    eprintln!("sd-phase {label} {{:.3}} ms", __sd_now.duration_since(__sd_phase).as_secs_f64() * 1000.0);\n'
            "    __sd_phase = __sd_now;\n"
        )
        source = source.replace(marker, code + marker, 1)
    tail = '    std::fs::write("/out/distances.txt", out).unwrap();'
    if source.count(tail) != 1:
        raise ValueError("Expected one output write; source has changed")
    source = source.replace(tail, tail +
        '\n    eprintln!("sd-phase output_write {:.3} ms", __sd_phase.elapsed().as_secs_f64() * 1000.0);', 1)
    return source

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=pathlib.Path, default=pathlib.Path("."))
    parser.add_argument("--out", type=pathlib.Path, required=True)
    args = parser.parse_args()
    src = args.repo / "track-d"
    dst = args.out
    if dst.resolve() == src.resolve():
        raise SystemExit("Refusing to overwrite the scored track directory")
    dst.mkdir(parents=True, exist_ok=True)
    (dst / "main.rs").write_text(instrument((src / "main.rs").read_text()))
    shutil.copyfile(src / "Dockerfile", dst / "Dockerfile")
    print(f"Local profiled Track D build context created: {dst}")
    print(f"Build with: docker build --network=none --target build -t sd-d-phases {dst}")
    print("The generated main.rs logs sd-phase timings to stderr; never submit it for scoring.")

if __name__ == "__main__":
    main()
