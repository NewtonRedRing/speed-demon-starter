# NewtonRedHeat performance record — Track D

## Selected source

Restored from previously evaluated NewtonRedHeat source blob `46bfcda6d77a5bfb6a72a4baf108de4d71c81a1c`. The build and source were selected to avoid publishing experimental implementations without provenance or established correctness.

## Recorded practice data

- Best documented machine points for the previously evaluated source: **68.316671**.
- Previously measured wall time: **historical radix result (runtime varies)**.
- Historical evidence: `previous radix-heap evaluation`.
- These historical values do **not** certify the new cleanup commit or a holdout run.

## Method

Parallel compact CSR graph construction; BFS locality ordering; sparse monotone radix-heap bidirectional shortest paths; full-width edge weights, exact lower-bound termination and deterministic output order.

## Validation

Use `python3 check.py d --data /path/to/full-input --expected <known-reference-sha256>`. Run the same check on independently generated tiny/sample/full datasets. A successful build alone is not a correctness proof. Official final score remains unknown.

## Final implementation notes

Only source within this track's Docker build context is evaluated; no precomputed outputs or data files are embedded in the runtime image. No external runtime packages are used. Preserve the existing MIT `LICENSE` and avoid claiming a new result until its run is observed.
