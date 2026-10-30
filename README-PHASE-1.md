# Speed Demon 2026 - Phase 1 Complete ✅

**Last Updated**: January 2026  
**Team**: NewtonRedHeat  
**Status**: Phase 1 Complete → Ready for Phase 2  
**Branch**: `speed-demon`  

---

## Quick Start for Phase 2

If you're just starting Phase 2, here's what you need to know:

### Current Score & Target
- **Current**: 33.267 pts overall
- **Target**: 72.054 pts (ALL STAR team)
- **Gap**: -38.787 pts (53.8% below)
- **Phase 2 Goal**: Reach 50-60 pts (36-65% improvement)

### What Works Already ✅
- Team registration complete (NewtonRedHeat on board)
- All 4 tracks compile and run correctly
- Compiler optimizations applied (LTO + march=native)
- All documentation complete

### What to Do Now
1. **Read these files** (in order):
   - `PHASE-2-TODO.md` - Detailed roadmap for each track
   - `PRE-PUSH-CHECKLIST.md` - Testing procedures before board push
   - `OPTIMIZATION-STRATEGY.md` - Root causes for each track

2. **Start with Track C** (highest impact: +10-15 pts)
   - Follow steps in `PHASE-2-TODO.md` section "Phase 2A"
   - Test locally with: `python3 check.py c --data data/c/tiny-ref`
   - Push only after local testing passes

3. **Then proceed to Tracks D, B, A** in order (each 2-3 hours)

4. **Expected outcome**: 50-60 pts overall after all Phase 2 work

---

## Understanding the Problem

### Why We're at 33 pts (Not 72)
The gap is **NOT due to language** (C++ can match Rust), but due to:

| Track | Root Cause | Current | Target | Impact |
|-------|-----------|---------|--------|--------|
| **A** | Single-threaded I/O (getline bottleneck) | 48.3 | 55-65 | +7-17 |
| **B** | 6-byte Occurrence structs in memory | 27.8 | 35-45 | +7-17 |
| **C** | 394 MB vs 13 MB target (unordered_set bloat) | 13.1 | 50-60 | **+37-47** ⭐ |
| **D** | Single-threaded Dijkstra (queries independent) | 27.4 | 35-40 | +7-13 |

**Key insight**: Track C alone can give +10-15 pts with memory optimization!

### What Makes Targets Fast
- **Track A** (79.8 pts Rust): Parallel I/O parsing + optimized buffer management
- **Track B** (65.7 pts Rust): Compact posting list representation  
- **Track C** (71.2 pts Rust): Efficient memory layout (13 MB, not 394 MB!)
- **Track D** (65.8 pts Rust): Bidirectional Dijkstra search

All fixable with C++ by using better data structures and parallelization.

---

## File Guide

### Essential Reading (Required)
1. **PHASE-2-TODO.md** ⭐ START HERE
   - Checklist for each track
   - Implementation options (with pros/cons)
   - Testing procedures
   - Time tracking

2. **PRE-PUSH-CHECKLIST.md**
   - Step-by-step procedure before board push
   - What to test locally
   - How to verify output correctness

3. **OPTIMIZATION-STRATEGY.md**
   - Detailed root cause analysis per track
   - Why each bottleneck exists
   - How other teams solved it

### Reference (For Context)
4. **IMPLEMENTATION-PHASE-1.md**
   - What was completed in Phase 1
   - Why compiler optimizations were chosen
   - Confidence levels for Phase 2

5. **SPEED-DEMON-GUIDE.md**
   - Competition rules and deadlines
   - Board push mechanics
   - Allowed languages and images

6. **BOARD-STATUS.md**
   - Current team position
   - How board scoring works
   - All 63 teams analyzed

### Current Session
7. **This file (README-PHASE-1.md)**
   - Quick reference for Phase 2 start

---

## Quick Reference: Track Priorities

### 🔴 PRIORITY 1: Track C (+10-15 pts)
```
Current Score: 13.136
Target Score: 50-60
Difficulty: Medium (memory optimization, exact output required)
Time: 3-4 hours
Confidence: 65%+
Strategy: Replace unordered_set<string> with efficient storage
```

### 🟠 PRIORITY 2: Track D (+5-8 pts)
```
Current Score: 27.364
Target Score: 35-40
Difficulty: Medium (requires parallelization)
Time: 2-3 hours
Confidence: 80%+
Strategy: Use 12-thread pool for independent Dijkstra runs
```

### 🟡 PRIORITY 3: Track B (+3-5 pts)
```
Current Score: 27.840
Target Score: 35-45
Difficulty: Easy (data structure optimization)
Time: 2-3 hours
Confidence: 80%+
Strategy: Compact 6-byte Occurrence struct to 4-5 bytes
```

### 🟢 PRIORITY 4: Track A (+2-3 pts)
```
Current Score: 48.326
Target Score: 55-65
Difficulty: Easy to Medium (I/O optimization)
Time: 2-3 hours
Confidence: 75%+
Strategy: Replace getline() with parallel buffered parsing
```

---

## How to Approach Phase 2

### The Testing Loop
```
1. Pick next task from PHASE-2-TODO.md
   ↓
2. Read implementation strategy
   ↓
3. Code the change (keep it focused)
   ↓
4. Compile locally: g++ -std=c++20 -O3 -DNDEBUG -flto -march=native -static -s ...
   ↓
5. Test with check.py: python3 check.py X --data data/X/tiny-ref
   ↓
6. If output mismatch, analyze or revert
   ↓
7. If output correct, test with full data: python3 check.py X --data data/X/full-seed1
   ↓
8. Commit with detailed message
   ↓
9. Push to speed-demon branch
   ↓
10. Wait 2-3 min for board results
    ↓
11. Check score improvement (expected +X pts)
    ↓
12. Repeat with next track
```

### Before Pushing to Board
Always follow PRE-PUSH-CHECKLIST.md:
- [ ] Code compiles without warnings
- [ ] Passes local tests with tiny-ref data
- [ ] Passes local tests with full-seed1 data
- [ ] Output hash verified with --expected flag
- [ ] Git commit message explains the change
- [ ] No unrelated files staged for commit

### If Score Regresses
- ❌ **Regressed > 5 pts**: Immediate revert
- ⚠️ **Regressed 2-5 pts**: Analyze root cause, try alternative approach
- ✅ **Same or improved**: Proceed to next track

---

## Key Commands

### Test Single Track
```bash
cd D:\challenge\speed-demon-starter
python3 check.py c --data data/c/tiny-ref
```

### Compile Locally
```bash
cd track-c
g++ -std=c++20 -O3 -DNDEBUG -flto -march=native -static -s -o solution src/main.cpp
```

### Test with Expected SHA256
```bash
python3 check.py c --data data/c/tiny-ref --expected [sha256-hash]
```

### Push to Board
```bash
git add track-c/src/main.cpp  # Or whatever files changed
git commit -m "Optimize Track C memory usage" -m "Detailed message here"
git push origin speed-demon
```

---

## Important Dates & Limits

| Event | Date/Time | Impact |
|-------|-----------|--------|
| **Phase 1 Complete** | Now ✅ | Baseline established (33.267 pts) |
| **Phase 2 Target** | Next 9-13 hours | Reach 50-60 pts |
| **Code Freeze** | Nov 2, 15:00 UTC | No more pushes after this |
| **Board Rate Limit** | 1 push per 5 min | ~420 total pushes available |
| **Time Buffer** | ~35 hours | Phase 1 + Phase 2 + Phase 3 |

---

## Success Indicators

✅ **Phase 1 was successful if:**
- Team is registered and visible on board
- Compiler optimizations compiled without errors
- All documentation is in git history

✅ **Phase 2 is successful if:**
- Track C reaches 40-50 pts (from 13.136)
- Track D reaches 35-40 pts (from 27.364)
- Track B reaches 35-45 pts (from 27.840)
- Track A reaches 55-65 pts (from 48.326)
- **Overall reaches 50-60 pts** (from 33.267)

⚠️ **If Phase 2 plateaus at < 50 pts:**
- Proceed to Phase 3 (advanced optimizations)
- May require bidirectional Dijkstra, SIMD, etc.

---

## Common Mistakes to Avoid

❌ **Don't** push changes to the board without local testing  
✅ **Do** always test locally with `check.py` first

❌ **Don't** make multiple unrelated changes in one commit  
✅ **Do** keep changes focused and document each clearly

❌ **Don't** continue if a track regresses significantly  
✅ **Do** revert immediately and try alternative approach

❌ **Don't** modify multiple tracks simultaneously  
✅ **Do** complete one track at a time for clear tracking

❌ **Don't** optimize without understanding the bottleneck  
✅ **Do** read OPTIMIZATION-STRATEGY.md first

---

## Questions?

Refer to these files:
- **"How do I test locally?"** → PRE-PUSH-CHECKLIST.md
- **"What should I optimize next?"** → PHASE-2-TODO.md
- **"Why is Track X slow?"** → OPTIMIZATION-STRATEGY.md
- **"What are the rules?"** → SPEED-DEMON-GUIDE.md
- **"Where does the team stand?"** → BOARD-STATUS.md

---

## Next Action

👉 **Read `PHASE-2-TODO.md` and start with Track C optimization**

Good luck! 🚀
