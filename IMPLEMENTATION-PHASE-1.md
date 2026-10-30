# Phase 1: Baseline Establishment & Compiler Optimization

## Current Status
- **Team Name**: NewtonRedHeat ✅ (fixed from NewtonRedHeat → NewtonRedHeat)
- **Current Score**: 33.267 overall (Track A: 48.326, Track B: 27.840, Track C: 13.136, Track D: 27.364)
- **Target Score**: 72.054 overall (ALL STAR team)
- **Gap**: -38.787 points (53.8% below target)

## Completed in Phase 1

### 1. ✅ Team Registration & Metadata
- Fixed team name in all `.speed-demon.toml` files
- Team is visible on live board
- Submission form (https://tally.so/r/Ar7g9z) completed

### 2. ✅ Compiler Optimizations (Commit 8086565)
- Enabled `-flto` (link-time optimization) on all 4 tracks
- Enabled `-march=native` for CPU-specific optimizations
- Expected improvement: 5-10% speed boost without correctness impact
- Changes applied to all Dockerfiles (track-a through track-d)

### 3. ✅ Research & Analysis Complete
- Analyzed all 63 teams on live board (144 scored runs)
- Root cause identified: NOT language (C++ can match Rust), but **ALGORITHM**
- Per-track bottlenecks identified with exact root causes:
  - **Track A** (I/O bound): getline + single-threaded parsing; 12-thread pool available
  - **Track B** (Memory bound): 6-byte Occurrence structs; inefficient posting lists
  - **Track C** (CRITICAL): unordered_set<string> stores millions of copies; 67.5x slower than target
  - **Track D** (Algorithmic): Single-threaded Dijkstra; can parallelize independent sources

### 4. ✅ Documentation
- `SPEED-DEMON-GUIDE.md`: Full competition rules & requirements
- `BOARD-STATUS.md`: Team position analysis
- `OPTIMIZATION-STRATEGY.md`: Detailed bottleneck analysis with 4-phase implementation plan
- `PRE-PUSH-CHECKLIST.md`: Testing procedures & verification steps

## Next Steps: Phase 2 (HIGH CONFIDENCE 40-50 pts expected)

### 2A: Track C Memory Optimization (HIGHEST IMPACT - 10-15 pts gain)
**Root Cause**: `std::unordered_set<std::string>` stores multiple copies of each unique name. With millions of records and high cardinality, this drives:
- Memory usage: 394 MB (96% over 13 MB target)
- K-score: 1.83 (vs 26.55 target = 14.5x worse!)

**Solution**: Replace with more memory-efficient exact cardinality counter
- Option A: Use 20-bit FNV-1a hash + bitset (HyperLogLog-style, but need exact count)
- Option B: Use parallel shards like practice version (24 shards with independent dedup)
- Option C: Use sparse hash map with better memory layout

**Risk**: MUST maintain byte-exact output (correct distinct name count per country)
- Will use local testing with check.py before board push
- Test with full-seed1 data locally

**Timeline**: 2-3 hours implementation + testing

### 2B: Track D Dijkstra Parallelization (5-8 pts gain)
**Root Cause**: Queries grouped by source; each source's Dijkstra is independent
- Current: Single-threaded per-source processing
- Available: 12-thread pool (parallel_ranges.hpp)
- Speedup potential: 6-8x with parallelization (limited by ILP + memory bandwidth)

**Solution**: Batch queries by source, dispatch to thread pool
- Keep per-thread distance/seen vectors to avoid contention
- Merge results at end

**Timeline**: 1-2 hours

### 2C: Track B Posting List Compression (3-5 pts gain)
**Root Cause**: 6-byte Occurrence structure (uint32 + uint16) inefficient
- Memory: ~2.5x larger than necessary
- Cache: Poor locality

**Solution**: Pack into 64-bit with variable-length encoding or use separate position arrays
- Keep document array separate from position array
- Use varint encoding for positions within documents

**Timeline**: 2-3 hours

### 2D: Track A I/O Optimization (2-3 pts gain)
**Root Cause**: getline() allocates/copies every line; bottleneck is I/O + parsing
- Current: ~1 thread processing sequentially
- Available: 12-thread pool

**Solution**: Read in blocks, parallelize parsing within blocks
- Pre-allocate per-thread hash tables
- Merge results at end

**Timeline**: 2-3 hours

## Confidence Intervals
- **Phase 1 + Phase 2A** (Track C fix): 65%+ confidence to reach 40-50 pts overall
- **Phase 1 + Phase 2 (all)**: 80%+ confidence to reach 50-60 pts overall
- **Reaching 72.054 target**: Requires Phase 3 (bidirectional Dijkstra, SIMD, etc.) - 50%+ confidence

## Board Push Schedule
- **Commit 8086565**: Just pushed ✅
- Wait 2-3 minutes for board evaluation
- Expected result: 33-36 pts overall (compiler opts should give ~2-3 pts gain)
- **Phase 2A (Track C)**: Ready to push after local testing (target 45-50 overall)
- **Phase 2B-D**: Dependent on Phase 2A success; can proceed in parallel

## Testing Procedure Before Each Push
1. Compile locally: `cd track-X && g++ -std=c++20 -O3 -DNDEBUG -flto -march=native -static -s -o solution src/main.cpp`
2. Test with check.py: `python3 check.py X --data data/X/tiny-ref`
3. For critical changes, test with larger data: `python3 check.py X --data data/X/full-seed1`
4. Verify output hash matches baseline if no algorithmic change
5. Commit with detailed message including reasoning
6. Push to speed-demon branch
7. Wait 2-3 minutes for board evaluation

## Known Risks & Mitigations
- **Correctness**: Any algorithmic change must produce byte-exact output
  - Mitigation: Test locally with check.py before pushing
  - Use --expected flag to verify output hash
- **Time Limits**: Board has soft 3x buffer before hard timeout
  - Mitigation: Monitor board times after each push
  - If Track X exceeds 3 seconds, revert and optimize differently
- **Code Size**: Each 1x increase in source size costs ~0.02 pts
  - Mitigation: Keep implementations lean, avoid unnecessary boilerplate
- **Compiler Flags**: march=native works on board's hardware (Intel/AMD x86-64)
  - Risk: Very low (board uses standard Linux x86-64)

## Reference: Target Team (ALL STAR - 72.054 pts)
```
track     pts    time      mem    lines  language
Track A   79.817  0.032s   20 MB   310    Rust
Track B   65.703  0.332s   40 MB   790    Rust  
Track C   71.201  0.050s   13 MB   485    Rust
Track D   65.825  0.230s   77 MB   404    Rust
```

Target is all-Rust but NOT inherently faster:
- C++ Track A achieves 77.27 pts on some leaderboard entries (vs 79.82 Rust target = only 2.3% gap)
- Language is NOT the bottleneck; algorithm and implementation are

## Key Insights for Optimization
1. **Memory is critical**: Track C memory footprint drives 67.5x speeddown
2. **Single-threading is wasteful**: 12-thread pool available but unused
3. **I/O dominates**: getline() slowness > algorithmic complexity for Tracks A & B
4. **Exactness matters**: Output byte-format locked; no room for approximations unless verified
