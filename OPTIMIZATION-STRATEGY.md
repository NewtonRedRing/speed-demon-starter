# 🎯 SPEED DEMON TARGET: 72.054 - RESEARCH & STRATEGY

**Target Team**: ALL STAR  
**Target Score**: 72.054 overall (geometric mean A, C, D)  
**Your Current Score**: 33.267 overall  
**Gap**: 2.17x (117% improvement needed)  
**Time Available**: ~35 hours to code freeze  

---

## KEY FINDING: This Is NOT a Language Problem

The research team extracted all 63 teams (144 scored runs) from the live board. Analysis shows:

**Language breakdown by track:**

| Track | Top 3 | Takeaway |
|-------|-------|----------|
| A | Rust 79.72, Rust 79.27, **C 78.40** | C++ within 2 points of Rust |
| B | Rust-dominated (top 4) | Rust teams dominate, but driven by algorithm, not language |
| C | **Rust 70.77, Rust 70.16, C++ 67.16** | C++ entry beats most Rust submissions! |
| D | Rust 66.67, Rust 65.42, Rust 60.75 | All-Rust top tier (selection effect, not hard ceiling) |

**Conclusion**: Language is NOT the bottleneck. C++ can match Rust. The gap is **algorithmic + parallel execution**.

---

## THE CRITICAL DISCOVERY: Unused Code Already In Your Repo

Your `track-*/src/` folders contain:
- `parallel_ranges.hpp/cpp` — **12-thread worker pool** (completely built, never used)
- `track_a_ingest.cpp` — Atomic hash table, lock-free slots (539 lines, never compiled)
- `track_b_index.cpp` — Advanced indexing (never compiled)
- `track_c_join.cpp` — Optimized join logic (never compiled)
- `metrics.hpp` — Performance tracking (never compiled)

**The Dockerfiles only compile `main.cpp`** (single-threaded reference).

Your 33.267 score is not C++ hitting its ceiling — it's a **single-threaded reference implementation** while you have multi-threaded code sitting right there unused.

---

## ROOT CAUSE ANALYSIS: Where The 2.17x Gap Comes From

### Scoring Formula (From Board Analysis)

```
Points ≈ 21.66·ln(K) + 0.15

where K = (efficiency factor) × (speed_ratio)^(~0.5) × (memory_ratio)^(~0.25) / (source_size)^(~0.02)
```

**Key insight**: Speed dominates (0.5 exponent). Memory matters (0.25 exponent). Source size is negligible (0.02 exponent).

### Per-Track Gap Analysis

| Track | Your Time | Target Time | Speedup | Memory | Speedup | K Gap | Fix Priority |
|-------|-----------|-------------|---------|--------|---------|-------|--------------|
| **A** | 0.628s | 0.032s | **19.6x** | 35MB → 20MB | 1.75x | 9.26 → 39.48 (+4.3x) | Medium |
| **B** | 6.275s | 0.332s | **18.9x** | 405MB → 40MB | **10.1x** | 3.60 → 20.61 (+5.7x) | High |
| **C** | 3.377s | 0.050s | **67.5x** ⚠️ | 394MB → 13MB | **30.3x** ⚠️ | 1.83 → 26.55 (+14.5x) ⚠️ | CRITICAL |
| **D** | 8.748s | 0.230s | **38x** | 116MB → 77MB | 1.5x | 3.53 → 20.73 (+5.9x) | High |

---

## ROOT CAUSE FOR EACH TRACK

### Track A: Ingest (3.1M log lines, 239 MB)

**Current code**:
- `std::getline` loop: allocates/copies every line into `std::string` ← I/O bottleneck
- `unordered_map<string, uint16_t>` for endpoint IDs
- **Zero parallelism** (despite `parallel_ranges.hpp` sitting unused)

**The 19.6x gap**:
- I/O overhead from string allocation per line
- No parallelism (12-thread pool available but unused)
- No SIMD for digit parsing

**Fix**:
1. Wire up `parallel_ranges.hpp` 12-thread pool
2. Chunk file into ~12 regions, parse in parallel with per-thread hash tables
3. Merge results at end
4. Expected gain: **5-10x** from parallelism alone

---

### Track B: Full-Text Index (253.8 MB docs + 1.8 MB queries)

**Current code**:
- Already uses smart two-pass approach (only index query-relevant terms)
- But: single-threaded full-corpus scan
- `unordered_map<string, vector<Occurrence>>` where `Occurrence` is 6 bytes per hit
- Drives 405MB footprint (2x input size)

**The 18.9x gap**:
- No parallelism on tokenization
- Inefficient posting-list format (6-byte struct per occurrence)
- No compression/varint encoding

**Fix**:
1. Parallelize corpus tokenization (12-way file sharding)
2. Replace `Occurrence{doc,pos}` struct with packed/varint format (drop positions if BOOL-only)
3. Expected gain: **5-8x** from parallelism + **2-3x** from memory compaction

---

### Track C: Record Join (201.9 MB records + 2.2 MB regions) — HIGHEST PRIORITY ⚠️

**Current code**:
```cpp
unordered_set<string> names;  // per country
// For each CSV row:
names.insert(row.customer_name);  // STRING ALLOCATION + HASH + HEAP
```

Every row allocates a full string copy, hashes it, stores in heap. Millions of rows × string overhead = **394 MB footprint**.

**The 67.5x gap (14.5x K-gap)**:
- This single data structure drives:
  - Memory: 394MB → 13MB (30x worse!)
  - Time: 3.377s → 0.050s (67x worse!)
  - K metric: 1.83 → 26.55 (14.5x worse!)

**This is your highest-ROI fix**: Single data structure change closes most of the gap.

**Fix**:
Replace `unordered_set<string>` with compact cardinality counter:
- Option 1: **Fixed-width hash + bitset**: Hash each name to 16-bit → track in bitset per country (2^16 bits = 8KB per country × ~250 countries = 2MB)
- Option 2: **HyperLogLog**: Approximate cardinality with ~5% error in negligible space
- Option 3: **Radix sort on hashes + run-length count**: Sort hashes, count unique

Expected gain: **30-40x** on this track alone (from current 3.377s to ~0.1s, memory 394MB → ~10MB)

---

### Track D: Shortest Path (92 MB graph + 29 KB queries)

**Current code**:
- Dijkstra per unique source (good algorithm)
- Binary heap via `push_heap`/`pop_heap`
- Early termination once targets found
- **Zero parallelism** (but per-source runs are embarrassingly parallel)

**The 38x gap**:
- Single-threaded Dijkstra
- Up to 6 P-cores available but unused
- Binary heap (cache-unfriendly for integer weights)

**Fix**:
1. Parallelize across unique source vertices (12-thread pool on independent heaps)
2. Swap binary heap for radix/bucket heap on integer edge weights
3. Bidirectional search for large target sets
4. Expected gain: **5-8x** from parallelism + **2-3x** from data structure choice

---

## SCORING IMPACT: Why Track C Is Your Leverage Point

Your current scoring system:
```
Overall = ∛(best 3 of 4 tracks)
Currently drops: Track C (weakest at 13.136)
Counted: A (48.326) × B (27.840) × D (27.364) = 33.267
```

If you fix Track C:
```
Old C: 13.136 (dropped)
New C: 50-60+ pts (beats D, enters counted top 3)
New breakdown: A (48.326) × C (55) × D (27.364) = 41-42 overall  ← +8-9 pts!
```

If you ALSO parallelize A/D:
```
New A: 60-70+ pts (19.6x speedup)
New D: 50-60+ pts (38x speedup)
New breakdown: C (55) × A (65) × D (55) = 58+ overall  ← +25 pts!
```

---

## REALISTIC 35-HOUR TIMELINE

### Phase 1: Wire Up Existing Multi-Threading (6-8 hours)

**Objective**: Use the `parallel_ranges.hpp` 12-thread pool already in your repo

1. **Track A** (easiest, 2-3 hours):
   - Chunk input file into 12 regions
   - Each thread parses its region into local hash table
   - Main thread merges results
   - Expected: 0.628s → 0.15-0.2s (**3-4x gain**)

2. **Track D** (moderate, 3-4 hours):
   - Group queries by source
   - Launch 1 Dijkstra per unique source on the 12-thread pool
   - Expected: 8.748s → 1.5-2s (**4-6x gain**)

3. **Track B** (moderate, 2-3 hours):
   - Tokenize corpus in parallel (12 file regions)
   - Merge posting lists from threads
   - Expected: 6.275s → 1.5-2s (**3-4x gain**)

**End of Phase 1 estimated score**: 38-42 overall ✅ Major jump from 33.267

---

### Phase 2: Fix Track C's Memory Bottleneck (4-6 hours)

**Objective**: Replace `unordered_set<string>` with compact cardinality structure

1. **Implement bitset-based name dedup** (3-4 hours):
   ```cpp
   // Instead of: unordered_set<string> names;
   // Use: bitset<65536> name_hash_bits;  // 8KB per country
   // Hash each name, set bit, count set bits at end
   ```

2. **Test locally with `check.py`** (1-2 hours):
   - Verify byte-for-byte output matches reference
   - Measure time (target: ~0.05s)

3. **Test on all 5 major size/shape variants** before pushing

**End of Phase 2 estimated score**: 50-55 overall ✅

---

### Phase 3: Secondary Optimizations (8-10 hours)

1. **Track B posting-list compression** (3-4 hours):
   - Pack postings with varint encoding
   - Drop position info if not needed
   - Expected gain: 2-3x additional

2. **mmap + SIMD for A/C digit parsing** (4-5 hours):
   - mmap input file instead of `getline` + string copy
   - SIMD digit parsing for numeric fields
   - Expected gain: 2x additional

3. **Track D: Radix heap + bidirectional search** (2-3 hours):
   - Radix heap for integer edge weights
   - Bidirectional Dijkstra for large target sets
   - Expected gain: 2-3x additional

**End of Phase 3 estimated score**: 60-68 overall ✅

---

### Phase 4: Polish & Iterate (8-10 hours buffer)

- Run board evaluations (1 per 5 min, so ~12 runs available per track)
- Profile on board to find remaining bottlenecks
- Tune parallelism parameters (threads, chunk sizes)
- Final verification that outputs are byte-for-byte correct

---

## EXPECTED SCORE TRAJECTORY

| Phase | Time Spent | Est. Overall | vs Target |
|-------|-----------|--------------|-----------|
| Current | 0h | 33.267 | -38.8 pts |
| Phase 1 (wire threading) | 8h | 40-42 | -30 pts |
| Phase 2 (Track C fix) | 6h | 50-55 | -17 pts |
| Phase 3 (secondary) | 10h | 60-68 | -4 to +4 pts |
| Phase 4 (iterate) | 10h | **65-72+** | **BEAT TARGET** ✅ |

**Confidence level**: Medium-High
- **You WILL beat current baseline (2.17x improvement)**: Very High (~95%)
- **You will reach 50+ (top quartile among scored teams)**: High (~85%)
- **You will beat 72.054 specifically**: Medium (~60%)

The last 10% to beat the very top teams is "last percentage point" optimization (radix heaps, advanced parallel patterns, micro-tuning) which is speculative in 35 hours.

---

## CRITICAL CONSTRAINTS

### Correctness is Byte-for-Byte

Every output must match the reference exactly (SHA-256 validation). **Any deviation → automatic 0 points for that track.**

Examples of what breaks:
- Track A: "Monday" vs "monday" (date format)
- Track B: Different sorting of equal-scoring results
- Track C: Floating-point rounding (if you change algorithm)
- Track D: Off-by-one in distance (if you change path preference)

**Always test locally first** with `check.py --expected <sha256>` before pushing.

---

### Rate-Limited Board Evaluation

- 1 push per team per 5 minutes
- 35 hours = 2100 minutes = ~420 possible board runs
- Per track = ~105 runs available (if spread evenly)
- This is plenty, but **use them wisely** — test locally first

---

## DECISION: C++ or Switch to Rust?

**Recommendation: Stay with C++**

Reasons:
1. C++ entries already match Rust on Track A & C (proof it's doable)
2. You have `parallel_ranges.hpp` (12-thread pool) already written in C++
3. 35 hours is not enough to rewrite all 4 tracks in Rust
4. High-risk: Rust rewrite could introduce correctness bugs with tight schedule

---

## ACTION CHECKLIST

### Before touching code:
- [ ] Read the existing `parallel_ranges.hpp` to understand the 12-thread pool API
- [ ] Study `main.cpp` for each track to understand input/output format
- [ ] Run `check.py` locally on current reference to verify you have baseline
- [ ] Measure current runtime locally (not just board time)

### Phase 1 (Threading):
- [ ] Track A: Implement file-chunking + parallel parse + merge
- [ ] Track D: Implement per-source Dijkstra + thread pool dispatch
- [ ] Track B: Implement parallel tokenization + posting list merge
- [ ] Local testing: All pass `check.py` with same output hash
- [ ] Push once per track, wait for board eval, log Run ID

### Phase 2 (Track C):
- [ ] Implement bitset-based name cardinality counter
- [ ] Replace `unordered_set<string>` in hot loop
- [ ] Local test extensively (most tricky semantic change)
- [ ] Push, verify output hash, measure time/memory

### Phase 3 (Secondary):
- [ ] Profile board results to see where time is still spent
- [ ] Implement mmap + SIMD if still time budget
- [ ] Radix heap for Track D if improvement visible

### Phase 4 (Final):
- [ ] Monitor board score progression
- [ ] At code freeze (Nov 2, 15:00 UTC): make repo public, stop coding

---

## Risk Mitigation

| Risk | Mitigation |
|------|-----------|
| Break correctness on Track C (most complex change) | Test locally 10x with different input shapes before pushing |
| Thread synchronization bug | Use `parallel_ranges.hpp` patterns (already vetted) rather than raw mutexes |
| Compiler incompatibility with Rust-style optimizations | Stick to C++17/20 standard library (no SIMD intrinsics unless tested) |
| Board evaluation timeout (3x reference) | Monitor time on board; if consistently over 0.5s, back off parallelism |
| Source code size penalty kills K metric | Research shows negligible (<0.02 coeff); not a constraint. More code for real speed = win. |

---

## Summary: DO NOT PUSH UNTIL

1. ✅ You've wired up `parallel_ranges.hpp` locally and verified it compiles
2. ✅ You've run `check.py` on all 4 tracks with current reference (baseline snapshot)
3. ✅ You've drafted the Track C bitset replacement (without submitting)
4. ✅ You've estimated what 40-42 overall would look like (Phase 1 target)
5. ✅ You're confident the parallelism won't break the 3x timeout constraint

**This prep work should take 2-3 hours.** Then and only then start Phase 1 pushes.

---

## Expected Final Push Timeline

- **Commit 1**: Phase 1 Threading (A/D/B) — Board eval in 2-5 min, expect 40-42 overall
- **Commit 2**: Phase 2 Track C fix — Board eval, expect 50-55 overall
- **Commits 3-N**: Phase 3 refinements, micro-tuning, iterate on slowest tracks
- **Nov 2, 14:50 UTC**: Final commit, make repo public, code freeze

**Target crossing**: 60-72+ overall, competitive with top teams, best 3 tracks in 50-70+ range each.

---

**Good luck! You have the code. You have the pool. You know what to fix. Now execute.** 🚀
