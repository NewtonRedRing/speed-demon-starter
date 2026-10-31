# Track B: exact Boolean support-trigger architecture — rejected after oracle benchmark

Date: 2026-10-31 08:35 UTC. This is **not** an official board result.

## Hypothesis

Avoid building all Boolean postings and evaluating 31,000 queries over
250,000-document bitmaps. Instead, precompute for each left-associative
Boolean query a small set of positive *support* terms, invert support
terms to query IDs, and process the query during the original document
scan only if one of its support terms occurs.

Support construction (exact):
- Begin with support = {first query term}.
- `AND t`: either old support or {t} is a valid support; choose the
  smaller by summed document frequency.
- `OR t`: support = old support union {t}.
- `NOT t`: retain old support.

For every document, each distinct support term triggers its associated
queries once. The query is evaluated left-to-right using the current
document's exact word-presence stamps; increment count, sum, and first-ten
document IDs. Every actual match must contain a support term, and
full evaluation prevents false positives. This requires no Boolean
posting lists, but common triggers can cause very many random updates.

## Reproducible synthetic results

C++20, GCC -O3 -march=native, 250k documents, 3k query terms, 10k
left-associative Boolean queries (2–6 terms; 50% AND, 30% OR, 20% NOT),
synthetic five-band term frequencies, independently computed bitset
oracles for every query, identical count/sum/first-ten output.

| Seed | Full bitset oracle | Sparse support candidate | Word-aligned support | Streaming trigger | Trigger events |
|---|---:|---:|---:|---:|---:|
| 2026 | 178 ms | 175 ms | 171 ms | 1,048 ms | 37.16 M |
| 719 | 182 ms | 177 ms | 167 ms | 971 ms | 37.70 M |
| 349 | 186 ms | 184 ms | 175 ms | 1,025 ms | 39.39 M |

These are isolated synthetic single-process timings; they do not include
text parsing or the official 50k-query workload. All oracle comparisons
passed on all three seeds.

**Conclusion:** the theoretically memory-free streaming trigger is
5–6x slower than an optimized query-wise bitset oracle due to tens of
millions of random query-state accesses. Sparse support and word-aligned
support improve the Boolean stage only 0–10% in this workload and add
code complexity. **Do not promote** any of these to `speed-demon` based
on these numbers.

## Next useful directions

Focus on the *document tokenization / phrase matching* hot path and
duplicate temporary posting memory, rather than changing Boolean
evaluation to per-document random query updates.

Draft PR #17 inline-eight posting storage remains a better memory
candidate; it still needs full public SHA-256 and whole-track benchmark.

The live board at 2026-10-31T08:18:11Z confirms competitor NickTrapp's
Track B 70.612375 points at 210.119 ms and 41.370 MB (run
`run-8dff97bc243e`), proving the target 69.556 is feasible with a
low-memory and fast architecture. NewtonRedHeat's best remains 53.122178
points at 537.941 ms and 300.159 MB (run `run-a1b2c119ee07`).
This does not reveal the competitor's implementation.

## Reproduction constraints

Prototype files are local artifacts:
- `b_stream_trigger.cpp` SHA-256:
  `40384782e5784eb9b4197d2c7f358f6f41b99d2503b7e2f2496acfb2ac1fba2d`
- `b_support_probe.cpp` SHA-256:
  `99f79b17863d91230e5427edd462099bdd5e472e3c519b951a824893e007e716`

No production branch changed. Official seed-1 data are linked from
https://speeddemon.dev/board/#datasets but this execution container
cannot resolve `s3.amazonaws.com` (DNS error), and GitHub Actions
runs fail before any executable steps or logs are emitted. Thus the
full SHA-256 gate remains unavailable here.
