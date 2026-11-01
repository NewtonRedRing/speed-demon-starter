# Track B — Index

Exact zero-dependency Rust implementation of the official Track B specification.

Queries are parsed first into a compact exact term dictionary. Corpus shards are
memory-mapped and tokenized with ASCII case folding; non-ASCII bytes remain
separators as required. The immutable dictionary uses SIMD control-byte probing,
CRC32C hashing and full token equality.

Boolean terms are accumulated as 64-document membership words and evaluated
with the required strict left-to-right operator semantics. Phrase queries use
exact token-ID windows around selected anchors; matches are deduplicated per
document and summarized as count, ID sum and first ten IDs. Twelve workers scan
aligned shards and their deterministic summaries are merged in document order.
Transparent huge pages are disabled to avoid board-side memory inflation.

Dependencies: none.

With another 24 hours: reduce per-token dictionary/state traffic and replace
repeated Boolean block work only when an exact full-dataset benchmark improves
the official K axes.

Compact shard-local `u16` postings are retained; Boolean list leaves decode
their document IDs on demand. Global `u32` materialisation increased peak
memory and lost a nine-pair alternating local comparison. Copy-on-write
Boolean operations and the 1/32 bitmap density threshold are unchanged.
Twelve logical shards remain necessary for the compact 15-bit local
doc-offset format, independently of the available worker count.

The retained candidate passed five final production-build official-input/output
checks. Final fresh-output local trimmed time fell from 364.180 ms to
331.222 ms and maximum observed cgroup peak from 93.155 MB to 55.194 MB.
These are VM measurements, not board measurements. The latest official result
is in `PERF.md`; the requested 106 ms/41 MB target remains unmet.

```powershell
python scripts\investigate_tracks.py --tracks b --runs 9 --variants baseline shard-posts --output .out\b-postings.json
```

The runner uses immutable baseline commit
`7a9886d0635e3deb74493f2c23cde76c30c521c2`, preserving its offline PGO
Dockerfile; it defaults to tmpfs output and validates every output hash.
See `PERF.md` for uncertainty and rejected alternatives.
