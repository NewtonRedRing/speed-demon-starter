# Architecture

Each engine uses file-backed input, bounded worker partitioning and deterministic
ordered output. The only required runtime dependencies are the C++ runtime, POSIX
filesystem/memory mapping support and pthreads.

## Log ingest
Disjoint byte ranges are aligned to newline boundaries. Per-worker endpoint
interning and compact latency records feed contiguous hour-major groups.
Exact nearest-rank p50/p95/p99 values use a sort for small groups and selection
for large groups. This parser is designed for the timestamp range and
record grammar documented by the dataset; validate those assumptions before
feeding arbitrary log schemas.

## Text search
Queries define the relevant vocabulary before corpus scanning. A sparse/dense
posting strategy handles Boolean operations; a token-level automaton recognizes
exact phrases as documents stream in. ASCII byte tokenization, left-to-right
Boolean evaluation and stable result ordering are intentional.

## CSV aggregate
Input is read-only mapped. Quote-parity preprocessing finds safe chunk
boundaries, including embedded newlines. Field decoding handles quoted commas,
doubled quotes and bytewise name identity. Integer cents eliminate floating
rounding error. A striped exact-name dictionary tracks country membership.

## Shortest paths
Forward and reverse compact CSR representations are constructed from the
directed edge list. Independent queries use bidirectional Dijkstra with
monotone radix queues, exact 64-bit distances and complete 32-bit edge weights.
No estimate or approximation substitutes for an actual shortest path.

## Operational constraints
- Linux is required for mmap, madvise and POSIX file handling.
- Formats and supported ranges are fixed contracts rather than autodetected.
- Processing is batch-oriented; input files are immutable during execution.
- Tests exercise correctness on small cases. Throughput must be measured with
  representative production data and production hardware.
