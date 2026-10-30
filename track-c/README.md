# Track C: Join

The task, the input files and the exact output format are in the [spec](https://github.com/hackathon-raptors/speed-demon-2026/blob/main/specs/c-join.md).

## Approach

The program parses CSV records with a byte-level quoted-field state machine, unescaping doubled quotes and counting embedded note bytes. It joins region IDs through a dense lookup, then aggregates exact integer cents and exact unescaped name values per country, including `_UNMATCHED`.

## Dependencies

None.

## With another 24 hours

Measure the board run, then profile distinct-name storage and CSV parsing.
