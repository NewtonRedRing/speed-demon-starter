# Track C — Join

Exact zero-dependency Rust implementation of the official hostile-CSV Track C
specification.

The input is memory-mapped and split into quote-safe ranges. The FALCON-C
parser scans consecutive 64-byte SIMD tiles rather than repeatedly classifying
a 128-byte window for every record. AVX2 identifies comma, newline and quote
bytes, while carry-less prefix XOR carries quote parity between tiles. Only
separators outside quoted fields are emitted. Quote counts at the name and
note boundaries give exact unescaped byte lengths without constructing CSV
strings; the original input bytes remain borrowed for exact name comparisons.
Both empty quoting styles, escaped quotes, embedded note newlines, CRLF/LF,
mid-record-boundary BOMs and long text fields are handled exactly. Numeric
fields are converted directly to integers. Region IDs use a dense lookup table
and numeric aggregation remains thread-local.

Distinct names use a fixed-capacity concurrent atomic table. Length and the
first/last eight raw bytes select a candidate bucket, never establish equality.
Slots store short names exactly; longer names store their first sixteen bytes
and compare the remaining bytes in an arena. CSV escaping is canonical for
nonempty names, so raw equality is exact decoded-value equality; both encodings
of an empty name use the same zero-length key.
Per-country membership is tracked with atomic bitsets so races cannot double
count a name. No probabilistic/hash-only equality is used.

Long names are compared byte-for-byte even on hash collisions. The final bytes
are copied behind 64 bytes of zero padding so SIMD loads never run past the
buffer, then parsed last. Transparent huge pages are disabled for sparse tables.
The offline build uses a PGO training stage on the allowlisted Rust image,
with the unmodified official generator and a separate training seed.

Dependencies: none.

With another 24 hours: profile dictionary / membership cache misses and test
wider structural tiles while preserving exact hash checks on multiple seeds.

The exact FALCON-C source has passed all official scoring gates. Its current
historical public-board best is **74.320728 machine points**, **36.405 ms**,
**10,354,688 bytes** peak memory and **K=30.648876** at
`run-6dcd8ad2012f`, commit `4b4f4259eb78d55be5f835337488a05c7add6ac6`.
An earlier exact-source run scored 73.956052 / 37.853 ms / 10,215,424 bytes
(`run-70f424aa22a8`); this variation occurred with the same algorithm.
The later exact batch=8/chunks=64 trial passed correctness but scored only
74.154112, so Oracle restored the above exact source in commit `ea2532f`.
Full per-seed evidence and reasons for rejection are in `PERF.md`.
