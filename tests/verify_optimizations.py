#!/usr/bin/env python3
"""Differential correctness + repeatable non-authoritative performance probe.

Run via .github/workflows/experiment-90-plus.yml on a GitHub-hosted runner.
The original speed-demon branch is compiled separately as an independent baseline.
The seed-1 sample hash additionally checks the official reference output.
"""
import argparse
import hashlib
import os
import pathlib
import statistics
import subprocess
import sys
import time

OUTPUTS = {"a": "summary.txt", "b": "results.txt", "c": "aggregates.txt", "d": "distances.txt"}
SAMPLE_SHA256 = {
    "a": "ca6311bbeb536fa55d85265bf6fd3d24cc9586f13de1ac73bd2a23ac3893aac3",
    "b": "eeeec1e9319da98a6f3c664c78f93e7078fd603c25c2bb3027174e81e3638a8f",
    "c": "9035fed36823232dd41ae2d1b9d6d21aa7c73f49d8e95647e953a89d5ebe9a66",
    "d": "a4c76ed030bc7e365db33191f6cf74708fba7f25eb48d64c8c4279f2e57046a0",
}
TINY_A_SHA256 = "c24fa507c963eaeac9224fd8435019b4f4e8c002ece7e32f316681a3957eb92f"


def checked(args):
    subprocess.run(args, check=True)


def execute(binary: str, output: pathlib.Path):
    output.unlink(missing_ok=True)
    start = time.perf_counter()
    checked([binary])
    seconds = time.perf_counter() - start
    digest = hashlib.file_digest(output.open("rb"), "sha256").hexdigest()
    return seconds, digest


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--track", choices=OUTPUTS, required=True)
    parser.add_argument("--generator", required=True)
    args = parser.parse_args()
    track = args.track
    output = pathlib.Path("/out") / OUTPUTS[track]

    for scale, seed in [("tiny", 1), ("tiny", 2), ("sample", 1)]:
        print(f"Generate {track} {scale} seed={seed}", flush=True)
        checked([sys.executable, args.generator, "--seed", str(seed), "--scale", scale, "--out", "/data"])

        baseline_time, baseline_digest = execute("/tmp/speed-demon-base", output)
        optimized_time, optimized_digest = execute("/tmp/speed-demon-candidate", output)
        assert optimized_digest == baseline_digest, (
            f"DIFFERENTIAL HASH FAILURE {track=} {scale=} {seed=}: "
            f"{baseline_digest} vs {optimized_digest}"
        )
        expected = SAMPLE_SHA256[track] if scale == "sample" else (
            TINY_A_SHA256 if scale == "tiny" and seed == 1 and track == "a" else None
        )
        if expected:
            assert optimized_digest == expected, (
                f"OFFICIAL HASH FAILURE {track=} {scale=} {seed=}: {optimized_digest} != {expected}"
            )
        print(f"PASS {track} {scale} seed={seed} sha256={optimized_digest} "
              f"baseline={baseline_time:.4f}s candidate={optimized_time:.4f}s", flush=True)

        if scale == "sample":
            # Warm caches then use medians, not a single lucky trial.
            base_times, candidate_times = [baseline_time], [optimized_time]
            for _ in range(3):
                base_times.append(execute("/tmp/speed-demon-base", output)[0])
                cand_time, cand_hash = execute("/tmp/speed-demon-candidate", output)
                assert cand_hash == expected
                candidate_times.append(cand_time)
            base_med = statistics.median(base_times)
            cand_med = statistics.median(candidate_times)
            ratio = base_med / cand_med if cand_med else float("inf")
            print(f"SAMPLE PERFORMANCE {track}: baseline_median={base_med:.6f}s "
                  f"candidate_median={cand_med:.6f}s speedup={ratio:.3f}x", flush=True)
            print("NOTE: GitHub-hosted sample timing is not an official full-scale "
                  "Speed Demon board score. Score 90+ cannot be confirmed here.", flush=True)


if __name__ == "__main__":
    main()
