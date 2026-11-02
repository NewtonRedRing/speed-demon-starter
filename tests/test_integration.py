#!/usr/bin/env python3
"""End-to-end regression tests of the installed command-line data engines."""
import argparse
import pathlib
import subprocess
import tempfile
import unittest

PARSER = argparse.ArgumentParser(add_help=False)
PARSER.add_argument("--bin-dir", type=pathlib.Path, required=True)
OPTS, _ = PARSER.parse_known_args()

class Engines(unittest.TestCase):
    def run_engine(self, executable, files, output, expected):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            data, out = root / "input", root / "output"
            data.mkdir()
            for name, content in files.items():
                (data / name).write_bytes(content.encode("utf-8"))
            cmd = [str((OPTS.bin_dir / executable).resolve()), "--input-dir",
                   str(data), "--output-dir", str(out), "--threads", "2"]
            proc = subprocess.run(cmd, capture_output=True, text=True, timeout=45)
            self.assertEqual(proc.returncode, 0, proc.stderr)
            self.assertEqual((out / output).read_text("utf-8"), expected)

    def test_ingest_empty(self):
        self.run_engine("log_ingest", {"requests.log": ""},
                        "summary.txt", "")

    def test_ingest_percentiles(self):
        requests = (
            "2026-10-30T18:00:00.000000Z POST / 499 0\n"
            "2026-11-02T17:00:00.000000Z HEAD /Z 100 0\n"
            "2026-11-02T17:59:59.999999Z OPTIONS /Z 599 4294967295\n"
        )
        expected = (
            "2026-10-30T18:00:00Z / 1 0 0 0 0 0\n"
            "2026-11-02T17:00:00Z /Z 2 0 4294967295 4294967295 1 500000\n"
        )
        self.run_engine("log_ingest", {"requests.log": requests},
                        "summary.txt", expected)

    def test_text_search(self):
        docs = "0\tHello world\n1\tHELLO there\n2\tworld hello\n3\tnothing\n"
        queries = ("BOOL hello\nPHRASE \"hello world\"\n"
                   "BOOL hello AND world\nBOOL hello NOT world\n"
                   "BOOL world OR nothing\n")
        expected = "3 3 0 1 2\n1 0 0\n2 2 0 2\n1 1 1\n3 5 0 2 3\n"
        self.run_engine("text_search", {"docs.txt": docs, "queries.txt": queries},
                        "results.txt", expected)

    def test_csv_aggregate(self):
        records = ("id,name,note,region_id,amount\n"
                   "1,alice,hello,10,2.50\n"
                   "2,alice,xx,10,-1.25\n"
                   "3,bob,ok,99,0.05\n")
        regions = "region_id,country\n10,USA\n"
        expected = ("USA 2 3 1.25 -1.25 2.50 1 10 7\n"
                    "_UNMATCHED 1 3 0.05 0.05 0.05 1 3 2\n")
        self.run_engine("csv_aggregate", {"records.csv": records, "regions.csv": regions},
                        "aggregates.txt", expected)

    def test_shortest_path(self):
        graph = "4 5\n0 1 2\n1 2 3\n0 2 10\n2 3 4\n0 3 100\n"
        queries = "0 3\n3 0\n2 2\n0 2\n"
        expected = "9\n-1\n0\n5\n"
        self.run_engine("shortest_path", {"graph.txt": graph, "queries.txt": queries},
                        "distances.txt", expected)

    def test_wide_edge_weights(self):
        graph = "3 3\n0 1 4294967295\n1 2 65536\n0 2 4294967294\n"
        queries = "0 2\n0 1\n"
        self.run_engine("shortest_path", {"graph.txt": graph, "queries.txt": queries},
                        "distances.txt", "4294967294\n4294967295\n")


    def test_randomized_shortest_paths(self):
        import heapq
        import random
        rng = random.Random(2049)
        for case in range(12):
            n = rng.randrange(3, 16)
            edges = [(rng.randrange(n), rng.randrange(n), rng.randrange(0, 100000))
                     for _ in range(n * 4)]
            queries = [(u, v) for u in range(n) for v in range(n)]
            adjacency = [[] for _ in range(n)]
            for u, v, weight in edges:
                adjacency[u].append((v, weight))

            def oracle(src, dst):
                distances = [float("inf")] * n
                distances[src] = 0
                heap = [(0, src)]
                while heap:
                    distance, u = heapq.heappop(heap)
                    if distance != distances[u]:
                        continue
                    if u == dst:
                        return distance
                    for v, weight in adjacency[u]:
                        candidate = distance + weight
                        if candidate < distances[v]:
                            distances[v] = candidate
                            heapq.heappush(heap, (candidate, v))
                return -1

            graph = f"{n} {len(edges)}\n" + "".join(
                f"{u} {v} {weight}\n" for u, v, weight in edges)
            pairs = "".join(f"{u} {v}\n" for u, v in queries)
            expected = "".join(f"{oracle(u, v)}\n" for u, v in queries)
            self.run_engine("shortest_path",
                            {"graph.txt": graph, "queries.txt": pairs},
                            "distances.txt", expected)

if __name__ == "__main__":
    unittest.main(argv=["test_integration.py"], verbosity=2)
