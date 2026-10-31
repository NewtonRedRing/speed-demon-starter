# Speed Demon 2026 — NewtonRedHeat

Contest submission for all four Speed Demon 2026 tracks.

## Submission state

- Submission branch: `speed-demon`
- Team: `NewtonRedHeat`
- Implementation language: **Rust on A, B, C and D**
- Runtime dependencies: **none**
- Licence: MIT
- Each track builds offline from digest-pinned images on the official allowlist.
- Each track writes exactly one required file under `/out`.
- No public-seed answers, fingerprints or precomputed outputs are embedded.

Each `track-a` through `track-d` contains only the contest-facing files:
`Dockerfile`, `.speed-demon.toml`, `Cargo.toml`, `main.rs`, `README.md`
and `PERF.md`.

The Dockerfiles compile directly with Rust 1.99.1-compatible `rustc`,
`--edition=2024`, LTO and `target-cpu=native`. The official board explicitly
builds native-CPU submissions on the scoring machine.

The root `check.py` is the published-style correctness helper and is not part
of any track's scored implementation.

Final scoring uses a hidden holdout seed from the same generators. The
implementations are exact and generic; public seed 1 is used only for validation
and live-board measurement.
