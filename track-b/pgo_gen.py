#!/usr/bin/env python3
"""NewtonRedHeat deterministic training corpus for profile-guided optimisation.

This produces only build-time synthetic inputs; no answers are embedded.
"""
import argparse
import pathlib
import random

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--seed", type=int, default=7)
    p.add_argument("--scale", choices=["tiny","sample","full"], default="sample")
    p.add_argument("--out", required=True)
    a = p.parse_args()
    rng = random.Random(a.seed)
    path = pathlib.Path(a.out)
    path.mkdir(parents=True, exist_ok=True)
    size = {"tiny": 1000, "sample": 25000, "full": 250000}[a.scale]
    queries = {"tiny": 1000, "sample": 5000, "full": 50000}[a.scale]
    vocab = ["the", "and", "in", "of", "estate", "bond", "bail", "court",
             "writ", "habeas", "corpus", "order", "surety", "matter",
             "notice", "motion", "filing", "section", "appeal", "claim"]
    vocab += ["term" + str(i) for i in range(10000)]
    with (path / "docs.txt").open("w", encoding="utf-8", newline="\n") as f:
        for i in range(size):
            n = rng.randrange(40, 161)
            words = [vocab[int(rng.paretovariate(1.25)) % len(vocab)] if rng.random() < 0.2
                     else vocab[rng.randrange(len(vocab))] for _ in range(n)]
            if i % 97 == 0:
                words.extend(["writ", "of", "habeas", "corpus"])
            f.write(str(i) + "\t" + " ".join(words) + "\n")
    with (path / "queries.txt").open("w", encoding="ascii", newline="\n") as f:
        for _ in range(queries):
            a, b, c = (rng.choice(vocab) for _ in range(3))
            v = rng.randrange(10)
            if v < 3:
                f.write(f'PHRASE "{a} {b}"\n')
            elif v < 6:
                f.write(f'BOOL {a} AND {b}\n')
            elif v < 8:
                f.write(f'BOOL {a} OR {b} NOT {c}\n')
            else:
                f.write(f'BOOL {a}\n')

if __name__ == "__main__":
    main()
