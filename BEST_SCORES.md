# Speed Demon — consolidated best-scored tracks

This commit combines the **exact directory trees** from the highest-scoring
verified NewtonRedHeat submissions on the public Speed Demon board feed
generated on 2026-11-01 at 13:00:25 UTC.

| Track | Scored source commit | Official public-seed-1 machine score | Time | Git tree |
|---|---|---:|---:|---|
| A — Ingest | `2ef49458a0aa68448c986763b146a8615eab6a83` | 80.334988 | 30.669 ms | `b1ec073b0dc9d5068a3212c105321862ee1801de` |
| B — Index | `778b883863da919e97d1186a23d5204347a53cd2` | 71.387395 | 198.553 ms | `24daa98a5da9d3a3b7ffa6aeab9d69d726d437ac` |
| C — Join | `c915724de42491e44b5d36308106f75efb4ea0b2` | 72.497352 | 44.062 ms | `08f0b4b5ef47e0881d311ddd490d6888d9e0a3f4` |
| D — Traverse | `6759b53eda0ced88ef30a8d0f9b77654a0c8686b` | 68.316671 | 174.011 ms | `aa782d132ba75c28da6a971e5a53456fb2fc8bfc` |

Source: https://s3.amazonaws.com/raptors.dev/speeddemon.dev/2026/board/board.json
Team: `NewtonRedHeat`, board slug `newtonredheat-dba6a3`.

**Important:** these are historical best verified results for each individual
directory. The combined commit must still be built, correctness-checked and
scored by the board or final judge. The board may measure different timings,
and this file makes no claim that the combined head was itself benchmarked.

The source directories were reattached by their Git tree SHAs, preserving
Dockerfiles, Rust code, manifests, and all track-local files unchanged.
