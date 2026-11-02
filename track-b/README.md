# Track B — Index

The Rust executable parses all queries first, creates an exact dictionary of query-relevant terms, and avoids building indexes for unrelated corpus tokens. Multiple threads tokenize disjoint document ranges; sparse posting lists and compact Boolean bitmaps support exact left-to-right Boolean evaluation. Phrase candidates use rare anchors followed by exact token-sequence verification. The answer contains the count, document-ID sum and first ten IDs.

- Language: Rust, no external runtime dependencies
- Inputs: `/data/docs.txt`, `/data/queries.txt`
- Output: `/out/results.txt`
- Memory discipline: compact postings and selective bitmap representation
- Build: two-phase training-data PGO and fat LTO
- Correctness: full token equality; hash values are never semantic identity

The source is the earlier NewtonRedHeat standalone implementation. Historical measurements are in `PERF.md`.
