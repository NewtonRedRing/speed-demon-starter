# High-Performance Data Engines

Four standalone, dependency-light C++20 command-line engines for structured log
analytics, Boolean/phrase text retrieval, CSV aggregation and directed shortest paths.

## Capabilities

| Executable | Purpose | Input files | Output file |
| --- | --- | --- | --- |
| `log_ingest` | Hourly endpoint latency percentiles and errors | `requests.log` | `summary.txt` |
| `text_search` | Boolean and ordered exact-phrase search | `docs.txt`, `queries.txt` | `results.txt` |
| `csv_aggregate` | Quote-aware CSV parsing and exact distinct-name aggregates | `records.csv`, `regions.csv` | `aggregates.txt` |
| `shortest_path` | Weighted, directed shortest-path queries | `graph.txt`, `queries.txt` | `distances.txt` |

Linux/x86-64 is the primary supported platform. Each tool reads a directory of
input files, creates the output directory if missing, writes one deterministic
result file, and reports failures on stderr with a nonzero exit code.

## Build

Requirements: CMake 3.20+, a C++20 compiler (GCC 12+ recommended), Python 3.9+
for tests, and pthreads.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Run

```bash
./build/log_ingest --input-dir data/log_ingest --output-dir out/log_ingest
./build/text_search --input-dir data/text_search --output-dir out/text_search
./build/csv_aggregate --input-dir data/csv_aggregate --output-dir out/csv_aggregate
./build/shortest_path --input-dir data/shortest_path --output-dir out/shortest_path
```

Use `--threads N` to cap worker count (recommended range: 1–12).
Environment overrides: `HPC_INPUT_DIR`, `HPC_OUTPUT_DIR`, `HPC_THREADS`;
command-line options take precedence.

## Try bundled examples

Small, readable fixtures with expected outputs live under `examples/`. For example:

```bash
./build/log_ingest --input-dir examples/log_ingest --output-dir out/log_ingest
diff examples/log_ingest/expected/summary.txt out/log_ingest/summary.txt
```

The examples exercise the documented file formats, not large-data performance.

## Data

`python3 scripts/download_data.py --engine all --dest data` downloads the
published synthetic evaluation inputs from their public object-storage location.
Each file is SHA-256 verified before it becomes visible under `data/`.
The complete collection is roughly 790 MB. Use `--engine csv_aggregate`,
for example, to download one collection. Network access is required only for
this optional development command, never for execution.

To produce a small subset of the downloaded input, run
`python3 scripts/make_public_sample.py --source data --dest sample_data`.
The separately triggered GitHub Actions **Verify public input corpus** workflow
also produces a downloadable sample artifact and verifies full output hashes.

Lightweight, constructed edge-case fixtures are embedded in the integration
test suite; no production dataset is checked into source control.

## Quality gates

```bash
ctest --test-dir build --output-on-failure
cmake -S . -B build-asan -DHPC_ENABLE_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-asan --parallel
ctest --test-dir build-asan --output-on-failure
```

CI compiles all engines and runs integration tests. The reference data downloader
verifies file integrity; it does not establish a particular runtime or throughput.

## Structure

`src/` contains independent C++ entry points, `include/` the common runtime
configuration, `tests/` end-to-end regression checks, `scripts/` data tooling,
and `docs/` design and operational notes. Build outputs and datasets are ignored.

Consult [architecture](docs/architecture.md), [operations](docs/operations.md),
and [validation](docs/validation.md) for implementation and deployment details.

## License

MIT. See [LICENSE](LICENSE).
