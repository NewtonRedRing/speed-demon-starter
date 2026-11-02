#!/usr/bin/env python3
"""Differential and official-worked-example checks for the experimental C solver.

Usage: cd track-d && cc -O3 -march=native -flto -pthread -std=c11 -o candidate candidate.c && python3 test_candidate.py
"""
import hashlib
import heapq
import pathlib
import random
import subprocess
import tempfile

BIN = pathlib.Path(__file__).resolve().parent / "candidate"
GRAPH = """8 12
0 1 4
1 2 3
0 2 9
2 0 5
2 1 6
1 3 0
3 4 0
4 4 7
4 5 4294967295
2 1 2
5 6 4294967295
0 1 4
"""
QUERIES = """0 2
2 1
1 4
4 4
7 7
0 6
4 5
6 0
0 7
1 0
2 1
"""
SHA = "2e9871bba1d5013144ceff48d806a28da0e3f203b29118a89d98d8f654c3f6a0"

def solve_ref(n, edges, s, t):
    adj = [[] for _ in range(n)]
    for u, v, w in edges:
        adj[u].append((v, w))
    distances = {s: 0}
    heap = [(0, s)]
    while heap:
        dist, u = heapq.heappop(heap)
        if dist != distances[u]:
            continue
        if u == t:
            return dist
        for v, w in adj[u]:
            new = dist + w
            if new < distances.get(v, 1 << 65):
                distances[v] = new
                heapq.heappush(heap, (new, v))
    return -1

def run(graph, queries):
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        (tmp / "out").mkdir()
        (tmp / "graph.txt").write_text(graph)
        (tmp / "queries.txt").write_text(queries)
        subprocess.run([str(BIN), str(tmp), str(tmp / "out" / "distances.txt")], check=True)
        return (tmp / "out" / "distances.txt").read_bytes()

assert hashlib.sha256(run(GRAPH, QUERIES)).hexdigest() == SHA
for n in (1, 2, 3, 8, 50, 100, 350, 2000):
    for seed in range(12):
        rng = random.Random(seed)
        weights = (0, 0, 1, 2, 11, 1023, 2046, 2047, 2048, 9999, 4294967295)
        edges = [(rng.randrange(n), rng.randrange(n), rng.choice(weights)) for _ in range(max(4*n,1))]
        pairs = [(rng.randrange(n),rng.randrange(n)) for _ in range(30)]
        pairs.extend(((0,0),(n-1,n-1),(0,n-1)))
        graph = f"{n} {len(edges)}\n" + "".join(f"{u} {v} {w}\n" for u,v,w in edges)
        queries = "".join(f"{u} {v}\n" for u,v in pairs)
        actual = [int(x) for x in run(graph, queries).splitlines()]
        expected = [solve_ref(n,edges,u,v) for u,v in pairs]
        assert actual == expected, (n, seed)
print("PASS official worked example + 96 random graph differential tests")
