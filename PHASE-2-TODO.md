# Phase 2: Algorithmic Optimizations - TODO Tracking

## Phase 2A: Track C Memory Optimization (PRIORITY 1 - START HERE)

### Task: Replace unordered_set<string> memory-inefficient storage

**Impact**: 10-15 pts gain (largest single-track opportunity)
**Current Score**: Track C: 13.136 pts → **Target**: 50-60 pts
**Root Cause**: Storing millions of string copies in unordered_set; 394 MB memory usage (96% over budget)

**Implementation Options** (in order of preference):
1. [ ] **Option A - Parallel Shards** (from practice version)
   - Divide records into 24 shards
   - Each shard processes independently with local dedup
   - Merge results at end
   - Pros: Proven (practice version uses this), memory-efficient, parallelization benefit
   - Cons: More complex code
   - Time: 3-4 hours

2. [ ] **Option B - Compact Hash Table**
   - Use custom hash table with better memory layout
   - Store only pointers to strings (not full copies initially)
   - Use string pool/arena allocation
   - Pros: Simpler than Option A, still efficient
   - Cons: Need to verify output correctness
   - Time: 2-3 hours

3. [ ] **Option C - HyperLogLog Approximate** (risky, only if exact not needed)
   - Use probabilistic cardinality counter
   - Pros: Extreme memory efficiency
   - Cons: Output won't match exactly, board will reject
   - Risk: VERY HIGH
   - Decision: **DO NOT USE** unless confirmed output can be approximate

**Testing Checklist**:
- [ ] Compiles locally without errors
- [ ] Passes `check.py c --data data/c/tiny-ref`
- [ ] Output hash matches baseline (no algorithmic change) OR explicitly differs per spec
- [ ] Test with `check.py c --data data/c/full-seed1` for full validation
- [ ] Memory usage < 100 MB (tracked via `--verbose` flag if available)
- [ ] Execution time < 2 seconds local (board has 3x soft limit)

**Status**: ⬜ NOT STARTED
**ETA**: Complete by: [TBD - 3-4 hours after start]

---

## Phase 2B: Track D Dijkstra Parallelization (PRIORITY 2)

### Task: Parallelize independent Dijkstra runs using 12-thread pool

**Impact**: 5-8 pts gain
**Current Score**: Track D: 27.364 pts → **Target**: 35-40 pts
**Root Cause**: Queries grouped by source; each source's Dijkstra independent but processed serially

**Implementation Steps**:
1. [ ] Study `parallel_ranges.hpp` API
   - [ ] Understand `parallelRanges(count, workers, work)` signature
   - [ ] Verify thread-safety of per-thread state vectors
2. [ ] Refactor main loop:
   - [ ] Group queries by unique source (already done in `source_queries` map)
   - [ ] Collect all sources into a vector
   - [ ] Dispatch to parallel_ranges instead of sequential loop
3. [ ] Per-thread state management:
   - [ ] Each thread gets its own distance[], seen[], heap[]
   - [ ] Allocate thread-local storage before parallelization
   - [ ] Clear between sources to avoid interference
4. [ ] Merge results:
   - [ ] Collect answers[query_index] from each thread
   - [ ] Ensure ordering matches original query sequence

**Testing Checklist**:
- [ ] Compiles with -pthread flag
- [ ] Passes `check.py d --data data/d/tiny-ref`
- [ ] Output matches baseline exactly (deterministic results)
- [ ] Speedup > 2x for parallel vs serial (validates threading benefits)
- [ ] No race conditions (run multiple times, verify output stable)

**Status**: ⬜ NOT STARTED
**ETA**: Complete by: [TBD - 2-3 hours after Track C]

---

## Phase 2C: Track B Posting List Compression (PRIORITY 3)

### Task: Optimize Occurrence storage from 6 bytes to 4-5 bytes per entry

**Impact**: 3-5 pts gain
**Current Score**: Track B: 27.840 pts → **Target**: 35-45 pts
**Root Cause**: 6-byte Occurrence struct (uint32 doc + uint16 pos) inefficient

**Implementation Options**:
1. [ ] **Option A - Packed 64-bit** (if K-value high enough)
   - Combine doc (32-bit) + pos (16-bit) into single uint64
   - Trade: 4 bytes saved but potentially worse cache locality
   - Time: 1 hour, low risk

2. [ ] **Option B - Varint Encoding**
   - Positions rarely exceed 65K, so 2-3 bytes sufficient
   - Use variable-length encoding
   - Decompress during search operations
   - Time: 2-3 hours, medium risk

3. [ ] **Option C - Separate Position Array**
   - Keep document array (uint32 per occurrence)
   - Move positions to separate uint16 array
   - Better cache locality, same size but different access pattern
   - Time: 1.5 hours, low risk

**Recommendation**: Start with Option A (simplest, least risk), escalate if needed

**Testing Checklist**:
- [ ] Compiles without errors
- [ ] Passes `check.py b --data data/b/tiny-ref`
- [ ] Output hash matches baseline (data structure is internal)
- [ ] Memory usage better than before (run locally with -Xmx flag)
- [ ] Execution time same or better

**Status**: ⬜ NOT STARTED
**ETA**: Complete by: [TBD - 2-3 hours after Track D]

---

## Phase 2D: Track A I/O Optimization + Parallelization (PRIORITY 4)

### Task: Replace getline() with buffered parsing + parallelize

**Impact**: 2-3 pts gain (smallest improvement, lower priority)
**Current Score**: Track A: 48.326 pts → **Target**: 55-65 pts
**Root Cause**: Single-threaded getline() parsing of large log file

**Implementation**:
1. [ ] Read entire file into memory
2. [ ] Divide into chunks (e.g., 8 regions for 8 threads)
3. [ ] Each thread:
   - [ ] Scans its region
   - [ ] Maintains per-thread hash tables of endpoints
   - [ ] Populates per-thread Group data structures
4. [ ] Merge results:
   - [ ] Deduplicate endpoint IDs
   - [ ] Concatenate Group data
   - [ ] Process as before

**Testing Checklist**:
- [ ] Compiles with -pthread flag
- [ ] Passes `check.py a --data data/a/tiny-ref`
- [ ] Output matches baseline exactly
- [ ] No memory explosions (parallelization shouldn't increase peak memory)

**Status**: ⬜ NOT STARTED
**ETA**: Complete by: [TBD - 2-3 hours after Track B]

---

## Post-Phase-2 Strategy

### Tracking Results After Each Push
```
Commit: [hash]
Track:  [A/B/C/D]
Before: [previous score]
After:  [new score]
Change: [+X or -Y]
Status: ✅ IMPROVED / ⚠️ SAME / ❌ REGRESSED

If regressed:
  - Revert immediately
  - Analyze root cause
  - Try alternative approach
```

### Phase 3 (If Phase 2 doesn't reach 60+ pts)
- **Track A**: Parallel I/O buffer scanning (10-20% speedup)
- **Track B**: Vectorized set operations / SIMD
- **Track C**: Parallel CSV parsing across shard boundaries
- **Track D**: Bidirectional Dijkstra (2-3x speedup on search)

### Phase 4 (Final Polish)
- Advanced cache optimizations
- SIMD/AVX usage where applicable
- Fine-tuning buffer sizes & thread counts

---

## Notes & Observations

### From Board Analysis
- C++ can match Rust (Track A has C++ entries at 77.27 pts vs Rust 79.82 = 2.3% gap)
- Memory is critical (Track C bottleneck is 394 MB vs 13 MB target)
- Parallelism underutilized (12-thread pool available but mostly unused)
- Language = ~5% of score difference, Algorithm = ~95%

### Success Criteria
- ✅ Phase 2A alone: 65%+ confidence to reach 40-50 pts overall
- ✅ Phase 2 (all): 80%+ confidence to reach 50-60 pts overall
- ⚠️ Target (72.054): 50%+ confidence (requires Phase 3)

### Risk Mitigation
- Always test locally before board push
- Use check.py --expected flag to verify output hash
- Commit detailed messages explaining each change
- If any track regresses > 2 pts, revert and try alternative

---

## Time Tracking

| Phase | Task | Estimated | Actual | Status |
|-------|------|-----------|--------|--------|
| 2A | Track C optimization | 3-4 hrs | - | ⬜ |
| 2B | Track D parallelization | 2-3 hrs | - | ⬜ |
| 2C | Track B compression | 2-3 hrs | - | ⬜ |
| 2D | Track A I/O + parallel | 2-3 hrs | - | ⬜ |
| **Total** | **All Phase 2** | **9-13 hrs** | - | **⬜** |

**Start Time**: [TBD]
**Checkpoint 1** (After 2A): Expect 40-50 pts overall
**Checkpoint 2** (After 2A+2B): Expect 45-55 pts overall
**Checkpoint 3** (After 2A+2B+2C): Expect 48-58 pts overall
**Target** (After 2A+2B+2C+2D): Expect 50-60 pts overall

---

Last Updated: 2026-01-01 (Phase 1 Complete)
Next Review: After baseline board push results
