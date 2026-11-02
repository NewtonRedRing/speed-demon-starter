# Validation strategy

## Automated checks
`ctest --test-dir build --output-on-failure` runs the Python integration
suite against native binaries using temporary input/output directories.
Native unit tests cover runtime argument parsing, directory handling and worker\nlimits. Seeded randomized graph tests compare against an independent Dijkstra\nreference for every source/destination pair on twelve generated graphs.\nAdditional cases cover empty log ingestion, timestamp grouping and percentiles, text
Boolean/phrase queries, CSV numeric aggregates and distinct names, directed
reachability, and full-width graph weights.

## Full dataset checks
Run `scripts/download_data.py` to obtain integrity-checked public synthetic
inputs. Run each binary over the corresponding collection and hash output files.
The publicly documented reference output digests are:

| Engine | Output SHA-256 |
|---|---|
| log_ingest | `495ca09682b9673ef836a506496177680bb4a3af54255174befddb1f83481cd3` |
| text_search | `f86973c210228141bcb6f1f1b10188ad2a9b65f71e7cd7cf4590b38de4e80927` |
| csv_aggregate | `d8088b1cd42ba1338b1345b107b55db7b96ef1f5066f3ff64fd26a8912a0f536` |
| shortest_path | `a23dbdf0a3e49cc8901310a9ee1dc6a9a569a85297b6da75c2a7002cb2ed277e` |

A passing build and small integration test does not substitute for comparison
against full data or an independent edge-case oracle. The reference digests
are acceptance targets, not claims that the current binaries have passed.
