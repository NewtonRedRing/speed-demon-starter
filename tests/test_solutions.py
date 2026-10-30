"""Independent exact-output oracles; run after building the four track images."""
import csv
import hashlib
import heapq
import io
import os
from pathlib import Path
import random
import re
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from benchmark import measure


class Solutions(unittest.TestCase):
    def verify(self, track, files, expected, runs=1):
        with tempfile.TemporaryDirectory(prefix="speed-demon-test-") as directory:
            data = Path(directory)
            for name, content in files.items():
                (data / name).write_bytes(content)
            digest = hashlib.sha256(expected).hexdigest()
            prefix = os.environ.get("SPEED_DEMON_TEST_TAG_PREFIX", "speeddemon-check/track-")
            result = measure(track, data, digest, runs, prefix + track)
            self.assertEqual(result["sha256"], digest)

    def test_a_empty(self):
        self.verify("a", {"requests.log": b""}, b"")

    def test_a_percentiles_boundaries_and_parallel_ranges(self):
        rng = random.Random(349)
        endpoints = ["/", "/x", "/x-export", "/x.csv", "/x/{id}", "/X", "/" + "a" * 124]
        dates = ["2026-10-30", "2026-10-31", "2026-11-01", "2026-11-02"]
        rows, groups = [], {}
        for i in range(42000):
            h = rng.randrange(72)
            date = dates[(h + 18) // 24]
            hour = (h + 18) % 24
            timestamp = f"{date}T{hour:02}:00:00.000000Z"
            endpoint = rng.choice(endpoints)
            status = rng.choice([100, 200, 499, 500, 599])
            latency = rng.choice([0, 4294967295, rng.randrange(100000)])
            rows.append(f"{timestamp} {rng.choice(['GET', 'OPTIONS', 'DELETE'])} {endpoint} {status} {latency}\n")
        rows.extend([
            "2026-10-30T18:00:00.000000Z GET /singleton 500 1\n",
            "2026-10-30T18:00:00.000000Z GET /pair 499 0\n",
            "2026-10-30T18:00:00.000000Z GET /pair 599 4294967295\n",
        ])
        for count in (3, 31, 32, 33):
            for value in range(count):
                rows.append(f"2026-10-30T18:00:00.000000Z GET /threshold-{count} 200 {count - value}\n")
        rows += rows[:10]
        rng.shuffle(rows)
        for row in rows:
            timestamp, _, endpoint, status, latency = row.split()
            group = groups.setdefault((timestamp[:13] + ":00:00Z", endpoint), [[], 0])
            group[0].append(int(latency))
            group[1] += 500 <= int(status) <= 599
        expected = []
        for (hour, endpoint), (values, errors) in sorted(groups.items()):
            values.sort()
            n = len(values)
            p = [values[(rank * n + 99) // 100 - 1] for rank in [50, 95, 99]]
            expected.append(f"{hour} {endpoint} {n} {p[0]} {p[1]} {p[2]} {errors} {errors * 1000000 // n}\n")
        self.verify("a", {"requests.log": "".join(rows).encode()}, "".join(expected).encode(), 2)

    def check_b(self, docs, queries):
        tokens = [re.findall(rb"[a-z0-9]+", doc.lower()) for doc in docs]
        index = {}
        for document, words in enumerate(tokens):
            for word in set(words):
                index.setdefault(word, set()).add(document)
        output = []
        for query in queries:
            if query.startswith("BOOL "):
                parts = query[5:].encode().split()
                result = index.get(parts[0], set()).copy()
                for i in range(1, len(parts), 2):
                    term = index.get(parts[i + 1], set())
                    if parts[i] == b"AND":
                        result &= term
                    elif parts[i] == b"OR":
                        result |= term
                    else:
                        result -= term
            else:
                phrase = query[8:-1].encode().split()
                candidates = set.intersection(*(index.get(word, set()) for word in phrase))
                result = {
                    d for d in candidates
                    if any(tokens[d][i:i + len(phrase)] == phrase
                           for i in range(len(tokens[d]) - len(phrase) + 1))
                }
            ids = sorted(result)
            output.append(" ".join(map(str, [len(ids), sum(ids), *ids[:10]])) + "\n")
        corpus = b"".join(str(i).encode() + b"\t" + doc + b"\n" for i, doc in enumerate(docs))
        self.verify("b", {"docs.txt": corpus, "queries.txt": ("\n".join(queries) + "\n").encode()},
                    "".join(output).encode())

    def test_b_unicode_phrases_overlap_and_boolean_order(self):
        docs = [
            "The surety posted bail; the BAIL BOND was forfeited.".encode(),
            "Writ of Habeas-Corpus: see 42 U.S.C. § 1983 (bail bond).".encode(),
            b"", "Zürich bail—bond, bail bail bond. Case 007. İSTANBUL 273K ﬁduciary".encode(),
            b"a b c a b b b and or not", b"b c", b"b b b b",
            b"a " + b"z" * 33 + b" c",
        ]
        queries = [
            'PHRASE "bail bond"', 'PHRASE "b c"', 'PHRASE "a b c"',
            'PHRASE "b b"', 'PHRASE "b b b"', 'PHRASE "b b b b b"',
            'PHRASE "a c"', 'PHRASE "bail bond"', 'PHRASE "z rich"',
            "BOOL a OR b AND c", "BOOL a NOT b OR c", "BOOL b NOT b",
            "BOOL and AND not", "BOOL absent OR bail", "BOOL 007 NOT 7",
            "BOOL zurich OR rich", "BOOL 273k OR 273", "BOOL cafe",
        ]
        self.check_b(docs, queries)

    def test_b_query_shapes_and_empty_corpus(self):
        for docs, queries in [
            ([], ["BOOL absent", 'PHRASE "absent term"']),
            ([b"a b a b", b"", b"a"], ['PHRASE "a b"', 'PHRASE "a b"']),
            ([b"a b a b", b"", b"a"], ["BOOL a", "BOOL a NOT b", "BOOL a OR absent"]),
        ]:
            with self.subTest(queries=queries):
                self.check_b(docs, queries)

    def test_b_dense_sparse_bitmap_merges_and_parallel_ranges(self):
        rng = random.Random(873)
        docs = []
        for i in range(16000):
            words = [b"common", b"a", b"b", b"c"] * 24
            words += [f"rare{rng.randrange(500)}".encode() for _ in range(10)]
            if i % 3 == 0:
                words += [b"b", b"b", b"b", b"b"]
            docs.append(b" ".join(words) if i % 97 else b"")
        queries = ['PHRASE "b b b"', 'PHRASE "a b c"', "BOOL common", "BOOL absent"]
        vocabulary = ["a", "b", "common", "absent", *[f"rare{i}" for i in range(500)]]
        for _ in range(150):
            query = "BOOL " + rng.choice(vocabulary)
            for _ in range(rng.randrange(1, 6)):
                query += " " + rng.choice(["AND", "OR", "NOT"]) + " " + rng.choice(vocabulary)
            queries.append(query)
        self.check_b(docs, queries)

    def test_c_exact_csv_names_boms_and_parallel_quote_boundaries(self):
        rng = random.Random(578)
        regions = {2: "AAA", 3: "AAA", 999999: "BBB", 1000001: "CCC", 7: "DDD"}
        names = ["", "Ann", "Ann ", 'Ann "Annie" Lee', "x,y", "\ufeffAnn", " spaced  "]
        notes = ["", "a,b", '"no"', 'say "no"\nthen wait', "\ufeffliteral", "a\nb\nc\nd"]
        records, aggregates = [], {}
        for i in range(42000):
            name = names[i % len(names)] if i % 5 == 0 else f"name {rng.randrange(2000)}" + " " * (i % 7)
            note = notes[i % len(notes)] + " ordinary words" * (i % 4)
            region = rng.choice([2, 3, 999999, 1000001, 4, 9999999])
            cents = rng.choice([-1, -99, 0, 9999999, rng.randrange(-99999, 10000000)])
            if region == 1000001:
                cents = -rng.randrange(1, 100)
            negative = "-" if cents < 0 else ""
            amount = f"{negative}{abs(cents) // 100}.{abs(cents) % 100:02}"
            row = [i * 3 + 1, name, note, region, amount]
            country = regions.get(region, "_UNMATCHED")
            group = aggregates.setdefault(country, [0, 0, 0, [], set(), 0, 0])
            group[0] += 1
            group[1] += row[0]
            group[2] += cents
            group[3].append(cents)
            group[4].add(name)
            group[5] += len(name.encode())
            group[6] += len(note.encode())
            stream = io.StringIO(newline="")
            csv.writer(stream, lineterminator="\r\n" if i % 2 else "\n").writerow(row)
            records.append(("\ufeff" if i % 37 == 0 else "") + stream.getvalue())
        records[-1] = records[-1].rstrip("\r\n")
        expected = []
        def money(cents):
            return f"{'-' if cents < 0 else ''}{abs(cents) // 100}.{abs(cents) % 100:02}"
        for country, (count, ids, total, amounts, distinct, name_bytes, note_bytes) in sorted(aggregates.items()):
            expected.append(f"{country} {count} {ids} {money(total)} {money(min(amounts))} "
                            f"{money(max(amounts))} {len(distinct)} {name_bytes} {note_bytes}\n")
        lookup = "region_id,country\n" + "".join(f"{r},{c}\n" for r, c in sorted(regions.items()))
        self.verify("c", {
            "regions.csv": lookup.encode(),
            "records.csv": ("\ufeffid,name,note,region_id,amount\r\n" + "".join(records)).encode(),
        }, "".join(expected).encode(), 3)

    def test_d_random_directed_graphs_and_wide_weights(self):
        for seed in range(4):
            with self.subTest(seed=seed):
                rng = random.Random(seed)
                n = 30
                edges = [(rng.randrange(n - 3), rng.randrange(n - 3),
                          rng.choice([0, 1, 2, 65534, 65535, 65536, 4294967295, rng.randrange(1000)]))
                         for _ in range(15000 if seed == 0 else 150)]
                edges.extend([(0, 1, 0), (0, 1, 500), (1, 2, 4294967295), (1, 2, 4294967295)])
                edges.extend([(27, 28, 4294967295), (28, 29, 4294967295), (29, 29, 0)])
                adjacency = [[] for _ in range(n)]
                for u, v, weight in edges:
                    adjacency[u].append((v, weight))
                queries, expected = [], []
                for source in range(n):
                    distances = [float("inf")] * n
                    distances[source] = 0
                    queue = [(0, source)]
                    while queue:
                        known, u = heapq.heappop(queue)
                        if known != distances[u]:
                            continue
                        for v, weight in adjacency[u]:
                            candidate = known + weight
                            if candidate < distances[v]:
                                distances[v] = candidate
                                heapq.heappush(queue, (candidate, v))
                    for target in range(n):
                        queries.append(f"{source} {target}\n")
                        expected.append(f"{distances[target] if distances[target] != float('inf') else -1}\n")
                graph = f"{n} {len(edges)}\n" + "".join(f"{u} {v} {w}\n" for u, v, w in edges)
                self.verify("d", {"graph.txt": graph.encode(), "queries.txt": "".join(queries).encode()},
                            "".join(expected).encode())

    def test_d_no_edges(self):
        self.verify("d", {"graph.txt": b"3 0\n", "queries.txt": b"0 1\n0 0\n2 1\n"}, b"-1\n0\n-1\n")


if __name__ == "__main__":
    unittest.main()
