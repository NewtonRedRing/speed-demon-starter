#!/usr/bin/env python3
"""NewtonRedHeat independent synthetic Track C PGO training input generator."""
import argparse
import pathlib
import random

def quoted(s):
    return '"' + s.replace('"', '""') + '"'

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--seed", type=int, default=271828)
    p.add_argument("--scale", choices=["tiny","sample","full"], default="sample")
    p.add_argument("--out", required=True)
    a = p.parse_args()
    rng = random.Random(a.seed)
    path = pathlib.Path(a.out)
    path.mkdir(parents=True, exist_ok=True)
    n = {"tiny": 1000, "sample": 100000, "full": 332000}[a.scale]
    nr = 2000 if a.scale == "tiny" else 200000
    countries = [chr(65 + i // 26) + chr(65 + i % 26) + "A" for i in range(24)]
    with (path / "regions.csv").open("w", encoding="ascii", newline="\n") as f:
        f.write("region_id,country\n")
        for i in range(1, nr + 1):
            f.write(f"{i},{countries[i % len(countries)]}\n")
    names = [f"Name {i}" for i in range(12000)] + ["Alice, Jane", 'Ann "Annie" Lee', "Betts "]
    notes = ["ordinary filing", "a note that\nspans two lines",
             'said "no" twice', "comma, quote and newline\ntogether", ""]
    with (path / "records.csv").open("wb") as f:
        f.write(b"\xef\xbb\xbfid,name,note,region_id,amount\r\n")
        for i in range(1, n + 1):
            name = rng.choice(names)
            note = rng.choice(notes)
            region = (nr + 11) if i % 251 == 0 else rng.randrange(1, nr + 1)
            cents = rng.randrange(-99999, 999999)
            minus = "-" if cents < 0 else ""
            money = f"{minus}{abs(cents)//100}.{abs(cents)%100:02d}"
            nv = "" if i % 131 == 0 else (quoted(name) if i % 137 else '""')
            tv = "" if i % 89 == 0 else quoted(note)
            bom = "\ufeff" if i % 12001 == 0 else ""
            ending = "\r\n" if i % 2 else "\n"
            line = f"{bom}{i},{nv},{tv},{region},{money}{ending}"
            f.write(line.encode("utf-8"))

if __name__ == "__main__":
    main()
