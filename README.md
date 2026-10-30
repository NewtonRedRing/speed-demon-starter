# Speed Demon 2026 — NewtonRedHat

Private C++20 submissions for all four tracks of [Speed Demon 2026](https://speeddemon.dev). Track code in `src/main.cpp` was written for the event after kickoff, from the published specs; no pre-event practice C++ source was copied into these folders.

## Start in five steps

1. The repository is registered for team `NewtonRedHat`, language `C/C++`.
2. The [Speed Demon Board app](https://github.com/apps/speed-demon-board) is installed for this private repo.
3. Push submissions to the `speed-demon` branch. Each changed track is built and checked by the board against the full public seed-1 dataset.
4. Record measured changes in each `PERF.md` with the board run ID. Code freeze is November 2, 15:00 UTC; make the repo public by then.

One repo per team holds all your tracks. Delete the folders of the tracks you are not entering. An unchanged folder is the reference itself, so the board reports it as unchanged and does not run it.

## What is in a track folder

| File | What it is |
|---|---|
| `Dockerfile` | Builds the folder offline, from an allowlisted image pinned by digest |
| `.speed-demon.toml` | Track, team, language, dependency count; set `team` |
| `PERF.md` | Your optimisation log, worth 8% of your total |
| `README.md` | Your approach, your dependencies, your next 24 hours |
| `src/main.cpp` | The C++20 track implementation |

## How your program runs

- It reads its inputs from `/data`, which is read-only, and writes exactly one file to `/out`, then exits 0. The file is `summary.txt` for A, `results.txt` for B, `aggregates.txt` for C and `distances.txt` for D.
- `TRACK` is set to `a`, `b`, `c` or `d`.
- There is no network, at build time or at run time. Commit your dependencies to the repo (`cargo vendor`, `go mod vendor` and so on).
- It runs as an unprivileged user on a read-only filesystem. `/tmp` is 1 GB of scratch space.
- It gets 12 hardware threads (six P-cores, turbo off), 8 GB of memory with no swap, and 512 processes.
- The live board has a 3x reference-time timeout; final scoring has a 10x timeout.
- Your output must match the reference's byte for byte (sha256) on every run.

## Check before you push

`check.py` builds a track offline and runs it with the board's Docker settings, then prints the output's sha256 and a rough time. You need Docker and Python 3.

```
python3 check.py a --data data/a
```

On Windows, `check.py` transfers data through temporary Docker volumes, so Docker
Desktop does not need a shared host drive. The same network, read-only, memory,
process and `/tmp` limits are applied to the run.

Seed 1 at `full` scale is the board's dataset. The full datasets and published input/output hashes are available from the [board](https://speeddemon.dev/board/). The separate sample release was removed.

```
git clone https://github.com/hackathon-raptors/speed-demon-2026
python3.13 speed-demon-2026/generators/a_ingest.py --seed 1 --scale full --out data/a
```

No Python 3.13? Run the generator in the same image the reference uses:

```
docker run --rm -v "$PWD:/w" -w /w python:3.13-slim python3 speed-demon-2026/generators/a_ingest.py --seed 1 --scale full --out data/a
```

Run the check once on the unchanged folder. The sha256 it prints is the correct answer for that data. After every change, pass it back, so the check fails on a wrong answer:

```
python3 check.py a --data data/a --expected <sha256>
```

The final score uses a hidden holdout seed. Passing the public seed is necessary, but does not guarantee the holdout result.

On Colima, keep the repo and the data under your home folder, or Docker cannot mount them.

## Your own language

Replace the folder's contents with your code and a Dockerfile that builds it offline. A two-stage build keeps the image small. For Rust with no crates:

```
FROM rust:1-slim-trixie@sha256:1a939748a7cbab172de91a014e3804a97af1394a8c844faa67fce9401c4e40fb AS build
WORKDIR /src
COPY . .
RUN cargo build --release --offline

FROM debian:13-slim@sha256:62bd423aeee912639ca8ee681c2b762cb3e25137e0a92b20be455898a9f034ad
COPY --from=build /src/target/release/solution /app/solution
ENTRYPOINT ["/app/solution"]
```

Here `solution` is the package name in your `Cargo.toml`. Every allowlisted image and its digest is in [allowlist.json](https://speeddemon.dev/allowlist.json). Each direct runtime dependency costs a little, so count them in `.speed-demon.toml`.

## What the board refuses before it builds

- A `FROM` that is not in the allowlist, or not pinned by digest.
- `ADD` of a URL or a git repo, and `COPY --from` an image (an earlier stage is fine).
- A `# syntax=` line; cache, secret or ssh mounts; `RUN --network`, `--security` or `--device`; `ONBUILD`.
- A build over 10 minutes, or an image over 2 GB.

## Scoring, briefly

Faster than the reference earns points. Using less memory helps, up to a cap. Code beyond the reference's size, and every dependency, cost a little. Each track folder is built and measured on its own: your other tracks never count toward it, and code shared between tracks is copied into each folder that uses it and counts in each. A run that fails correctness, the timeout or the sandbox scores nothing. The full formula and the judging criteria are on speeddemon.dev.

## Licence

The reference code is copyright Hackathon Raptors CIC, under the MIT licence in `LICENSE`. Your repo has to be public under an OSI-approved licence at code freeze; keeping MIT is the simplest way.
