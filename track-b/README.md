# Track B: Index

The task, the input files and the exact output format are in the [spec](https://github.com/hackathon-raptors/speed-demon-2026/blob/main/specs/b-index.md).

## Approach

The program tokenizes document bytes using ASCII-only lowercase and indexes only query terms, retaining document postings and token positions. Boolean operators are applied strictly left to right with sorted-set operations. Phrase matches are verified at consecutive token positions. Each result reports count, id sum and the ten smallest matching ids.

## Dependencies

None.

## With another 24 hours

Measure the board run, then profile index construction, postings memory and phrase verification.
