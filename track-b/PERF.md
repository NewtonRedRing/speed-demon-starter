# NewtonRedHeat performance record — Track B

## Selected source

Restored from previously evaluated NewtonRedHeat source blob `c7dc80d3413beb6d0303bc376fe95e816a2b7b1c`. The build and source were selected to avoid publishing experimental implementations without provenance or established correctness.

## Recorded practice data

- Best documented machine points for the previously evaluated source: **72.172141**.
- Previously measured wall time: **183.883 ms**.
- Historical evidence: `run-f979478c0277`.
- These historical values do **not** certify the new cleanup commit or a holdout run.

## Method

Query-dependent selective indexing; mixed sparse sorted postings and Boolean bitmaps; anchor-verified exact phrases; source builds with PGO and fat LTO. Profile input is generated at build time, not embedded in the runtime image.

## Validation

Use `python3 check.py b --data /path/to/full-input --expected <known-reference-sha256>`. Run the same check on independently generated tiny/sample/full datasets. A successful build alone is not a correctness proof. Official final score remains unknown.

## Final implementation notes

Only source within this track's Docker build context is evaluated; no precomputed outputs or data files are embedded in the runtime image. No external runtime packages are used. Preserve the existing MIT `LICENSE` and avoid claiming a new result until its run is observed.
