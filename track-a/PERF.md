# NewtonRedHeat performance record — Track A

## Selected source

Restored from previously evaluated NewtonRedHeat source blob `4559a57b037785452dcf64f4e098456440f96cd8`. The build and source were selected to avoid publishing experimental implementations without provenance or established correctness.

## Recorded practice data

- Best documented machine points for the previously evaluated source: **84.279208**.
- Previously measured wall time: **21.662 ms**.
- Historical evidence: `run-d45aa4ea3d05`.
- These historical values do **not** certify the new cleanup commit or a holdout run.

## Method

Line-aligned parallel memory-mapped parsing; packed four-byte records; shared endpoint dictionary with full equality checks; chronological hour grouping and exact nearest-rank latency percentiles.

## Validation

Use `python3 check.py a --data /path/to/full-input --expected <known-reference-sha256>`. Run the same check on independently generated tiny/sample/full datasets. A successful build alone is not a correctness proof. Official final score remains unknown.

## Final implementation notes

Only source within this track's Docker build context is evaluated; no precomputed outputs or data files are embedded in the runtime image. No external runtime packages are used. Preserve the existing MIT `LICENSE` and avoid claiming a new result until its run is observed.
