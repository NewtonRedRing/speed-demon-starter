"""Verify every graph line belongs to exactly one mapped byte shard."""
from random import Random

rng = Random(20261031)


def partition_rows(data, header_length, workers):
    rows = []
    size = len(data)
    for worker in range(workers):
        first = header_length + (size - header_length) * worker // workers
        last = header_length + (size - header_length) * (worker + 1) // workers
        if first and data[first - 1] != 10:
            while first < size and data[first] != 10:
                first += 1
            first += first < size
        while first < last:
            stop = data.find(b"\n", first)
            if stop == -1:
                raise AssertionError("missing graph newline")
            rows.append(tuple(map(int, data[first:stop].split(b" "))))
            first = stop + 1
    return rows


checked = 0
for _ in range(30):
    n = rng.randrange(1, 7000)
    graph = [(rng.randrange(50000), rng.randrange(50000),
              rng.choice((0, 1, 65535, 65536, 4294967295,
                          rng.randrange(1000000)))) for _ in range(n)]
    header = f"50000 {n}\n".encode()
    data = header + b"".join(f"{u} {v} {w}\n".encode()
                              for u, v, w in graph)
    for workers in (1, 2, 3, 6, 8, 12, 16, 41):
        assert partition_rows(data, len(header), workers) == graph
        checked += 1
print(f"PASS {checked} exact random graph shard-layout checks")
