#!/usr/bin/env python3
"""Create a bounded set of valid example inputs from downloaded source data.

Output is small and safe to attach as a CI artifact. CSV records are processed
as logical rows (never truncated mid-quote); graph edge counts are corrected.
"""
import argparse
import csv
from pathlib import Path

def take_lines(source, target, count):
    target.parent.mkdir(parents=True, exist_ok=True)
    with source.open("rb") as src, target.open("wb") as dst:
        for _ in range(count):
            line = src.readline()
            if not line:
                break
            dst.write(line)

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source", type=Path, default=Path("data"))
    p.add_argument("--dest", type=Path, default=Path("sample_data"))
    args = p.parse_args()
    src, dst = args.source, args.dest
    take_lines(src / "log_ingest/requests.log", dst / "log_ingest/requests.log", 400)
    take_lines(src / "text_search/docs.txt", dst / "text_search/docs.txt", 300)
    take_lines(src / "text_search/queries.txt", dst / "text_search/queries.txt", 50)
    target = dst / "csv_aggregate"
    target.mkdir(parents=True, exist_ok=True)
    with (src / "csv_aggregate/regions.csv").open("rb") as f:
        (target / "regions.csv").write_bytes(f.read())
    with (src / "csv_aggregate/records.csv").open("r", encoding="utf-8-sig", newline="") as input_file, (target / "records.csv").open("w", encoding="utf-8", newline="") as output_file:
        reader, writer = csv.reader(input_file), csv.writer(output_file, lineterminator="\n")
        for index, record in enumerate(reader):
            if index >= 301:
                break
            writer.writerow(record)
    target = dst / "shortest_path"
    target.mkdir(parents=True, exist_ok=True)
    with (src / "shortest_path/graph.txt").open("rb") as f:
        header = f.readline().split()
        if len(header) != 2:
            raise ValueError("invalid graph header")
        n = int(header[0])
        edges = [f.readline() for _ in range(1200)]
        edges = [line for line in edges if line]
    with (target / "graph.txt").open("wb") as f:
        f.write(f"{n} {len(edges)}\n".encode())
        f.writelines(edges)
    take_lines(src / "shortest_path/queries.txt", target / "queries.txt", 50)
    print("Created sample inputs at", dst)

if __name__ == "__main__":
    main()
