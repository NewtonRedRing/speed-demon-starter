# Track A: Ingest

The task, the input files and the exact output format are in the [spec](https://github.com/hackathon-raptors/speed-demon-2026/blob/main/specs/a-ingest.md).

## Approach

The program parses each log row once, groups latencies by UTC hour and exact endpoint bytes, and sorts each group's latency values for nearest-rank percentiles. It writes groups in chronological-hour and bytewise-endpoint order. Status codes from 500 through 599 count as errors.

## Dependencies

None.

## With another 24 hours

Measure the board run, then profile parsing and per-group sorting before making targeted changes.
