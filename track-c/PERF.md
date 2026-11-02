# NewtonRedHeat performance record — Track C

## Selected source

Restored from previously evaluated NewtonRedHeat source blob `e6782b241cd8f07bca5b114fdb05019a483f91e7`. The build and source were selected to avoid publishing experimental implementations without provenance or established correctness.

## Recorded practice data

- Best documented machine points for the previously evaluated source: **74.320728**.
- Previously measured wall time: **36.405 ms**.
- Historical evidence: `run-6dcd8ad2012f`.
- These historical values do **not** certify the new cleanup commit or a holdout run.

## Method

AVX2 CSV tile classification; quote-state preservation; dense region lookup; integer-cents aggregation; exact-bytes distinct name table with per-country membership bits; PGO training on generated input.

## Validation

Use `python3 check.py c --data /path/to/full-input --expected <known-reference-sha256>`. Run the same check on independently generated tiny/sample/full datasets. A successful build alone is not a correctness proof. Official final score remains unknown.

## Final implementation notes

Only source within this track's Docker build context is evaluated; no precomputed outputs or data files are embedded in the runtime image. No external runtime packages are used. Preserve the existing MIT `LICENSE` and avoid claiming a new result until its run is observed.
