# Operations

## Deployment
Build Release binaries with a pinned compiler and archive the versioned build
artifacts separately from source. Run the tools as an unprivileged user with
read-only input permissions and write permissions limited to the output folder.

## Inputs and outputs
All executables accept `--input-dir`, `--output-dir` and `--threads`.
The input directory must contain the required filenames as listed in the README.
An existing output filename is overwritten upon a successful run; do not run
two processes targeting the same output simultaneously.

## Error handling
Invalid options, missing files and malformed input yield nonzero process status
and a diagnostic on stderr. Monitor both exit status and the presence of
expected output files. Output is not transactional: for atomic publication,
run into a dedicated staging output directory and rename only after success.

## Performance profiling
Prefer comparable end-to-end measurements with input parsing, construction,
query evaluation and output included. Use `/usr/bin/time -v`, `perf stat`,
and input-file SHA-256 fingerprints; run repeated warm/cold passes and report
median, p95, process RSS and compiler flags. Compare against the last
correctness-verified baseline, not against stale microbenchmarks.

## Resource sizing
Worker count defaults to up to twelve for large inputs and one for small
inputs. Override it with `--threads` when memory or CPU quota is constrained.
Memory maps are read-only and may be dropped as processing progresses.
