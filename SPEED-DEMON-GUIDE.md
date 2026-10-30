# Speed Demon 2026 - Complete Reference Guide

## Quick Links
- **Main Site**: https://speeddemon.dev/
- **Kickstart Guide**: https://speeddemon.dev/kickstart/
- **Live Board**: https://speeddemon.dev/board/
- **Registration Form**: https://tally.so/r/Ar7g9z (REQUIRED - DO THIS NOW!)
- **Allowed Images**: https://speeddemon.dev/allowlist.json
- **Datasets**: https://speeddemon.dev/board/ (see DATA section)
- **Rules**: https://speeddemon.dev/rules/

---

## Your Current Status

### ✅ Completed:
1. ✅ Repository created from template
2. ✅ Board app installed (read-only access)
3. ✅ Code pushed to speed-demon branch (commit a882458)
4. ✅ Team name fixed to "NewtonRedHeat" in all .speed-demon.toml files
5. ✅ All 4 tracks optimized and compiling
6. ✅ MIT License in place

### ⏳ TODO - BLOCKING:
- **🚨 REGISTER TEAM**: Fill https://tally.so/r/Ar7g9z with:
  - Team Name: `NewtonRedHeat`
  - Repository: `https://github.com/NewtonRedRing/speed-demon-starter`
  - Tracks: `A, B, C, D`
  - Language: `C/C++`

---

## The 5 Official Steps (From Kickstart)

### 1. Use the Template ✅
- Open: https://github.com/hackathon-raptors/speed-demon-starter
- Click "Use this template"
- Can stay private until code freeze
- **Your Status**: DONE

### 2. Install the Board App ✅
- Install: Speed Demon Board app on your repo
- Provides: Read-only access to speed-demon branch
- **Your Status**: DONE

### 3. Register Your Team ⏳ CRITICAL
- Fill: https://tally.so/r/Ar7g9z
- One form per team
- **Your Status**: PENDING

### 4. Push to speed-demon ✅
- Default branch: `speed-demon`
- Command: `git push` (pushes to default)
- Board builds: Every `track-*` folder
- Results appear: Within minutes on board
- **Your Status**: DONE (commit a882458)

### 5. Make it Fast ⏳ ONGOING
- Log every step in track's `PERF.md`
- Include board run ID for each measurement
- Code freeze: Nov 2, 15:00 UTC
- At freeze: Make repo public under OSI license
- **Your Status**: Ready to begin (MIT license already in place)

---

## Competition Structure

### Timeline
- **Now**: Oct 30, 18:58 UTC
- **Register**: Immediately
- **Code Freeze**: Nov 2, 15:00 UTC
- **Time Remaining**: ~35 hours for optimization

### How Scoring Works
- **Overall Score**: Geometric mean of best 3 tracks
- **Each Track**: Scored on time, memory, correctness
- **Current Baseline**: 33.267 points (unchanged reference code)
- **Competitive Target**: 55+ points
- **First Place**: 70+ points

### Scoring Components
- 80%: Machine performance (time + memory)
- 12%: Correctness (byte-for-byte output match)
- 8%: PERF.md documentation with run IDs

---

## Track Descriptions & Resources

### Track A: Ingest (Log Aggregation)
- **Input**: requests.log (238.8 MB, 3.1M lines)
- **Output**: summary.txt
- **Reference Time**: 0.628 s
- **Reference Memory**: 35 MB
- **Data**: https://s3.amazonaws.com/raptors.dev/speeddemon.dev/2026/data/track-a/full-seed1/requests.log

### Track B: Full-Text Index & Search
- **Input**: docs.txt (253.8 MB), queries.txt (1.8 MB)
- **Output**: results.txt
- **Reference Time**: 6.275 s
- **Reference Memory**: 405 MB
- **Data**: https://s3.amazonaws.com/raptors.dev/speeddemon.dev/2026/data/track-b/full-seed1/

### Track C: Record Join & Aggregation
- **Input**: records.csv (201.9 MB), regions.csv (2.2 MB)
- **Output**: aggregates.txt
- **Reference Time**: 3.377 s
- **Reference Memory**: 394 MB
- **Data**: https://s3.amazonaws.com/raptors.dev/speeddemon.dev/2026/data/track-c/full-seed1/

### Track D: Shortest Path (Graph Traversal)
- **Input**: graph.txt (92.0 MB), queries.txt (29 KB)
- **Output**: distances.txt
- **Reference Time**: 8.748 s
- **Reference Memory**: 116 MB
- **Data**: https://s3.amazonaws.com/raptors.dev/speeddemon.dev/2026/data/track-d/full-seed1/

### Download All Data
- **Total**: 790.5 MB
- **Hashes (JSON)**: https://s3.amazonaws.com/raptors.dev/speeddemon.dev/2026/data/SHA256SUMS.json
- **Hashes (Text)**: https://s3.amazonaws.com/raptors.dev/speeddemon.dev/2026/data/SHA256SUMS.txt

---

## Program Contract & Constraints

### Program Interface
```
INPUT:  /data (read-only directory)
OUTPUT: Single file to /out (then exit 0)
ENV:    TRACK variable set to 'a', 'b', 'c', or 'd'
```

### Resource Limits (On Evaluation Machine)
| Resource | Limit |
|----------|-------|
| CPU | 12 threads · 6 P-cores · turbo off |
| Memory | 8 GB · no swap |
| Processes | 512 |
| Scratch | /tmp · 1 GB tmpfs |
| Network | NONE (build and runtime) |
| Filesystem | read-only /data · write one file to /out |

### Time Limits
| Limit | Value |
|-------|-------|
| Board Cap | 3x the reference time |
| Final Cap | 10x the reference time |
| Example (Track D) | Reference 8.748s → Limit 87.48s |

### Correctness Requirement
- Output must match reference **byte-for-byte**
- Validated via SHA-256 hash
- Wrong output = FAILED (0 points for that track)

### Evaluation Machine
- **Location**: Falkenstein (fsn1)
- **Processor**: i5-13500
- **Threads**: 12 (6 P-cores · 8 E-cores)
- **Memory**: 8 GB
- **Turbo**: OFF
- **OS**: Linux

---

## Docker & Build Requirements

### Allowed Base Images (with pinned digests)

| Language | Image | Digest Use |
|----------|-------|-----------|
| Python | python:3.13-slim | sha256:d98cc29d3266e535dd9da1099688d9cb3ca4753813571e071763d17725460962 | Reference |
| C/C++ | gcc:15 | sha256:eb5e64286ad47187b32bda75c6b6916166ecb41da56559b8b09e0001748d2cce | All versions |
| C/C++ | gcc:16 | sha256:43978d7710a803d1c3d32f4117c1528ec7285a4ce2c0a51e982ec1a6e3d87d95 | Newer |
| Rust | rust:1-slim-trixie | sha256:1a939748a7cbab172de91a014e3804a97af1394a8c844faa67fce9401c4e40fb | Minimal |
| Rust | rust:1-trixie | sha256:931c2c0531a8b143e044f0f3914dfe802f15bb3d1955f0b657e9359327ac2ffc | Full toolchain |
| Rust+LLVM | halloumee/speed-demon-rust:1.99.1-trixie-llvm-tools | sha256:13e4ee7135d8eac6c56476c7d1aee201f36479f4745fd5d0e6b4529f4e13251a | PGO support |
| Go | golang:1-trixie | sha256:fbb7ae40b62ff8afe31662df570d39b4f5052ac32b90dae8e60d7657a20a74f4 | Latest |
| Java | eclipse-temurin:25-jdk | sha256:2d5d5e9b533342a565907c84f47a2ec298c40cde7b13a3ca85d4950fa456a641 | Build |
| Java | eclipse-temurin:25-jre | sha256:dbb43d81099eec91e0800226ed2296d03146ee7857165b95112372ad0d67e87f | Runtime |
| C# / F# | mcr.microsoft.com/dotnet/sdk:10.0 | sha256:747cbade5bfd918a175ac32f8f1d931edec2c7d0d5397a3de72eaef1ac1f66d2 | Build |
| C# / F# | mcr.microsoft.com/dotnet/runtime:10.0 | sha256:0cf09aaccf6a9f68900aed4c73fb28fe25bb59ec85de4dea08ba02240a8635f3 | Runtime |
| Node.js | node:22-slim | sha256:de170b39439eb0e5a3444ad7d60b015614a381e10fecae2e30ec9e735122f38d | v22 |
| Node.js | node:24-slim | sha256:55fb5f5a527ea2c5c4a95f963e48ef90aa38c7b346054fdc8ee9957a2b55d205 | v24 |
| Swift | swift:6.2 | sha256:d6f8f6112e9a63da0a4a151d6b93acd16699d35c0b82531ac1a245f5af210197 | Latest |
| Haskell | haskell:9 | sha256:15aeb965021bf51853f95a2328291d169aa541c836ca76986b97297df817f293 | GHC 9 |
| GraalVM | ghcr.io/graalvm/native-image-community:25 | sha256:0d936f32bb8acb5bc60c41b33e05f064d7a6aaf36b726538296c54949bd4a3c0 | Native |
| Runtime | debian:13-slim | sha256:62bd423aeee912639ca8ee681c2b762cb3e25137e0a92b20be455898a9f034ad | Minimal |

**Full list**: https://speeddemon.dev/allowlist.json

### Dockerfile Rules
✅ **Allowed**:
- Two-stage builds (FROM base AS build, then FROM runtime)
- COPY from earlier stages
- Standard RUN/COPY commands

❌ **NOT Allowed**:
- FROM images not on allowlist
- FROM images without digest pinning
- ADD of URLs or git repos
- COPY --from external images
- `# syntax=` directive
- Cache, secret, or SSH mounts
- RUN --network, --security, --device flags
- ONBUILD instructions
- Builds over 10 minutes
- Images over 2 GB

---

## What Each Track Folder Needs

### Required Files

| File | Purpose |
|------|---------|
| `Dockerfile` | Builds from allowlisted image (pinned digest) |
| `.speed-demon.toml` | Metadata: track, team, language, dependency_count |
| `PERF.md` | Optimization log (8% of score) |
| `README.md` | Your approach, dependencies, next optimization ideas |
| Your code | Replace reference; write one file to /out |

### .speed-demon.toml Example (Current)
```toml
track = "a"
team = "NewtonRedHeat"
language = "C/C++"
dependency_count = 0
```

### PERF.md Format
```markdown
# Track A Performance Log

| Step | Change | Run ID | Time (s) | Memory (MB) | Points |
|------|--------|--------|----------|-------------|--------|
| 1 | I/O buffering | run_abc123 | 0.628 | 35 | 48.326 |
| 2 | Your next optimization | run_xyz789 | ? | ? | ? |
```

---

## Optimization Workflow

### Per Cycle (Repeat 35 times or until code freeze)

1. **Make a change** in `track-*/src/main.cpp`
2. **Test locally**:
   ```bash
   g++ -std=c++20 -O3 -DNDEBUG main.cpp -o solution
   ```
3. **Commit and push**:
   ```bash
   git add .
   git commit -m "Optimization: <description>"
   git push origin speed-demon
   ```
4. **Board evaluates** (2-5 minutes)
5. **Check board** at https://speeddemon.dev/board/ for your team
6. **Copy Run ID** from board results
7. **Edit PERF.md**:
   ```markdown
   | N | Your optimization | run_id | time | memory | points |
   ```
8. **Commit documentation**:
   ```bash
   git add track-a/PERF.md
   git commit -m "Log: optimization results"
   git push origin speed-demon
   ```
9. **Repeat** for next optimization

---

## Before You Push: Local Testing

### Generate Test Data
```bash
git clone https://github.com/hackathon-raptors/speed-demon-2026
python3.13 speed-demon-2026/generators/a_ingest.py --seed 1 --scale sample --out data/a
```

### Verify Output
```bash
# First run - learn the correct hash
python3 check.py a --data data/a

# Subsequent runs - verify your code produces same output
python3 check.py a --data data/a --expected <sha256>
```

### Tips
- On Colima, keep repo and data under home folder for Docker mounting
- Always check locally first before pushing to board
- Wrong output = FAILED with 0 points
- Test multiple times to ensure consistency

---

## Live Board Details

### Board URL
https://speeddemon.dev/board/

### Status Meanings
| Status | Meaning |
|--------|---------|
| queued | Waiting turn (1 per team per 5 min) |
| building | Docker image building offline |
| queued to run | Built, waiting for single runner |
| running | Currently executing timed run |
| scored | Results posted (lime number) |
| failed | Wrong output / timeout / sandbox issue |
| unchanged | Using starter template (no run) |
| idle | Nothing in flight |

### Demon Levels
- **1-30**: Based on machine score
- **25-30**: ★ (top demon levels)
- **31-50**: ✦ (cosmic levels - past full marks, K > 100)
- **needs 3**: Fewer than 3 tracks scored (no overall yet)
- **♛**: Top three overall ranking

### Raw Data Feed
- JSON: https://s3.amazonaws.com/raptors.dev/speeddemon.dev/2026/board/board.json
- Updated live during competition

---

## Key Dates & Deadlines

| Date/Time | Event |
|-----------|-------|
| Oct 30, 18:58 UTC | NOW - Register team! |
| Oct 30, 15:00 UTC | Datasets published (+ 3h 58m ago) |
| Nov 2, 15:00 UTC | CODE FREEZE (hard stop) |
| ~35 hours | Time to optimize |

**CRITICAL**: No submissions accepted after Nov 2, 15:00 UTC

---

## Important Reminders

### One Repo Per Team
- Single repository for all tracks
- Tracks as `track-a/`, `track-b/`, `track-c/`, `track-d/`
- On `speed-demon` branch (default)

### Track Independence
- Each track scored separately
- Other tracks never count toward it
- Shared code copied into each folder (counts in each)

### Template Handling
- Unchanged folders (reference code) don't run
- Board reports them as "unchanged"
- Delete folders you're not entering

### Correctness is Mandatory
- Output must match reference byte-for-byte
- SHA-256 validation
- Wrong output = 0 points (automatic fail)

### Code Freeze Requirements
- Nov 2, 15:00 UTC: No more code changes
- Must make repo PUBLIC
- Must have OSI-approved license (MIT is fine ✓)

---

## Troubleshooting

### Board Not Showing My Team
- Check team name exactly matches form: `NewtonRedHeat`
- Verify all `.speed-demon.toml` files have correct name
- Ensure code compiles locally first
- Confirm board app has repo access
- Try pushing a small change to trigger rebuild

### Code Compiles but Board Shows "failed"
- Output must be byte-for-byte identical to reference
- Check output file name (summary.txt, results.txt, etc.)
- Ensure program exits with code 0
- Verify no writing to stdout/stderr
- Test locally with `check.py` first

### Build Takes Too Long
- Board caps builds at 10 minutes
- Optimize Dockerfile (use slimmer base images)
- Cache dependencies properly
- Avoid unnecessary compilations

### Image Too Large
- Board rejects images > 2 GB
- Use multi-stage builds
- Use slim base images (debian:13-slim, python:3.13-slim)
- Strip unnecessary dependencies

---

## Resources & Links

| Resource | Link |
|----------|------|
| Main Site | https://speeddemon.dev/ |
| Kickstart Guide | https://speeddemon.dev/kickstart/ |
| Live Board | https://speeddemon.dev/board/ |
| Rules | https://speeddemon.dev/rules/ |
| Registration | https://tally.so/r/Ar7g9z |
| Allowed Images | https://speeddemon.dev/allowlist.json |
| Template Repo | https://github.com/hackathon-raptors/speed-demon-starter |
| Generators | https://github.com/hackathon-raptors/speed-demon-2026 |
| Board Feed (JSON) | https://s3.amazonaws.com/raptors.dev/speeddemon.dev/2026/board/board.json |
| Datasets | https://speeddemon.dev/board/ (DATA section) |
| SHA256 Hashes (JSON) | https://s3.amazonaws.com/raptors.dev/speeddemon.dev/2026/data/SHA256SUMS.json |
| SHA256 Hashes (Text) | https://s3.amazonaws.com/raptors.dev/speeddemon.dev/2026/data/SHA256SUMS.txt |

---

## Next Steps

1. 🚨 **REGISTER**: Fill https://tally.so/r/Ar7g9z immediately
2. ⏰ **WAIT**: Board re-evaluates your team (2-5 minutes)
3. 📊 **MONITOR**: Check https://speeddemon.dev/board/ for NewtonRedHeat
4. 📝 **DOCUMENT**: Record Run IDs in PERF.md files
5. 🚀 **OPTIMIZE**: Code → Push → Wait → Log → Repeat
6. ⏱️ **FREEZE**: Nov 2, 15:00 UTC - Make repo public and stop coding

**Good luck! Every millisecond is a soul.** ✨
