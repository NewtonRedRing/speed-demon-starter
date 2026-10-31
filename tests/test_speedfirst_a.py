"""Independent Track A correctness stress test for experimental branches."""
from collections import defaultdict
from datetime import datetime, timedelta, timezone
from pathlib import Path
import random
import subprocess

INPUT = Path("/data/requests.log")
OUTPUT = Path("/out/summary.txt")
START = datetime(2026, 10, 30, 18, tzinfo=timezone.utc)
METHODS = ("GET", "POST", "PUT", "PATCH", "DELETE", "HEAD", "OPTIONS")
ENDPOINTS = [
    "/", "/health", "/v2/orders", "/v2/orders-export",
    "/v2/orders.csv", "/v2/orders/{id}", "/" + "a" * 124
] + [f"/e/{i}" for i in range(1835)]
assert len(ENDPOINTS) == len(set(ENDPOINTS)) == 1842


def run(seed, count):
    rng = random.Random(seed)
    groups = defaultdict(lambda: [[], 0])
    rows = []
    for i in range(count):
        h = rng.randrange(72)
        dt = START + timedelta(hours=h)
        endpoint = ENDPOINTS[i] if i < len(ENDPOINTS) else (
            ENDPOINTS[rng.randrange(24)] if rng.randrange(4)
            else ENDPOINTS[rng.randrange(len(ENDPOINTS))]
        )
        status = rng.choice((100, 200, 201, 404, 499, 500, 503, 599))
        latency = rng.choice((0, 4294967295, rng.randrange(0, 1000000)))
        when = dt.strftime("%Y-%m-%dT%H:00:00Z")
        stamp = dt.strftime("%Y-%m-%dT%H:%M:%S.000000Z")
        row = f"{stamp} {METHODS[i % len(METHODS)]} {endpoint} {status} {latency}\n"
        rows.append(row)
        values, error_count = groups[(when, endpoint)]
        values.append(latency)
        groups[(when, endpoint)][1] = error_count + (status >= 500)
    rng.shuffle(rows)
    INPUT.write_bytes("".join(rows).encode())
    expected = bytearray()
    for (hour, endpoint), (values, errors) in sorted(groups.items()):
        values.sort()
        n = len(values)
        p50, p95, p99 = (values[(p * n + 99) // 100 - 1] for p in (50, 95, 99))
        expected.extend(
            f"{hour} {endpoint} {n} {p50} {p95} {p99} {errors} "
            f"{errors * 1000000 // n}\n".encode()
        )
    subprocess.run(["./solution"], check=True)
    actual = OUTPUT.read_bytes()
    assert actual == expected, (
        f"seed={seed} count={count}: expected {len(expected)} bytes,"
        f" got {len(actual)} bytes; first difference at "
        f"{next((i for i, pair in enumerate(zip(expected, actual)) if pair[0] != pair[1]), 'EOF')}"
    )
    print(f"PASS seed={seed}, records={count}, groups={len(groups)}")


if __name__ == "__main__":
    INPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    INPUT.write_bytes(b"")
    subprocess.run(["./solution"], check=True)
    assert OUTPUT.read_bytes() == b""
    for args in ((1, 1842), (3, 100000), (17, 160000)):
        run(*args)
