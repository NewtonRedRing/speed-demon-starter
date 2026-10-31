"""Independent randomized quote-parity oracle for mapped and buffered scans."""
from random import Random

rng = Random(20261031)
for i in range(10000):
    data = bytes(rng.choice((10, 13, 32, 34, 44, 65, 66, 67))
                 for _ in range(rng.randrange(800)))
    a, b = sorted((rng.randrange(len(data) + 1),
                   rng.randrange(len(data) + 1)))
    buffered = 0
    for p in range(a, b, 128):
        buffered ^= data[p:min(p + 128, b)].count(b'"') & 1
    mapped = data[a:b].count(b'"') & 1
    assert buffered == mapped
print("PASS 10,000 quote-parity byte-range comparisons")
