# ✅ PRE-PUSH CHECKLIST - Do NOT Commit Until Complete

**Purpose**: Validate all changes locally before pushing to board (rate-limited to 1 push per 5 min)

**Status**: ⏳ PREPARATION PHASE - Not ready to push yet

---

## Phase 0: Preparation (2-3 hours) - DO FIRST

### Understanding the Codebase

- [ ] Read `D:\challenge\speed-demon-starter\track-a\src\parallel_ranges.hpp`
  - [ ] Understand the 12-thread worker pool API
  - [ ] Find example usage in current code or comments
  - [ ] Verify it compiles with current setup

- [ ] Read `D:\challenge\speed-demon-starter\track-a\src\main.cpp`
  - [ ] Understand input format (CSV, log lines, graph, etc.)
  - [ ] Identify output format and file path
  - [ ] Note any data structure used (hash maps, vectors, etc.)

- [ ] Repeat for Track B, C, D `main.cpp` files
  - [ ] Map out hot loops (where time is spent)
  - [ ] Identify data structures that allocate per-input (candidates for optimization)

### Baseline Measurement

- [ ] Download `check.py` from https://github.com/hackathon-raptors/speed-demon-2026
- [ ] Generate sample data for Track A:
  ```bash
  git clone https://github.com/hackathon-raptors/speed-demon-2026
  python3.13 speed-demon-2026/generators/a_ingest.py --seed 1 --scale sample --out data/a
  ```

- [ ] Run current reference code locally:
  ```bash
  cd D:\challenge\speed-demon-starter
  python3 check.py a --data data/a
  ```
  - [ ] Record the SHA-256 output hash (baseline)
  - [ ] Record execution time (baseline)
  - [ ] Verify "PASS" status

- [ ] Repeat for B, C, D with their respective data

### Understand the Scoring Formula

- [ ] Read "OPTIMIZATION-STRATEGY.md" section on scoring formula
- [ ] Understand why Track C's unordered_set<string> is the bottleneck
- [ ] Understand the parallelism opportunity (12-thread pool)

---

## Phase 1: Track C Memory Fix (First Optimization)

### Analyze Current Code

- [ ] Open `track-c/src/main.cpp`
- [ ] Locate the line: `unordered_set<string> names;` (or similar per-country dedup)
- [ ] Trace where it's used:
  - [ ] How many times is `.insert()` called per run? (~millions for CSV)
  - [ ] What does output depend on? (count of distinct names per country)
  - [ ] Is positional information needed? (for output format)

### Design the Bitset Replacement

- [ ] Option 1: Fixed-width hash + bitset (recommended)
  ```cpp
  unordered_map<string, bitset<65536>> country_names;
  // Instead of storing strings, hash each name to 16-bit, set bit
  // At end, count set bits: std::bitcount(bitset) or popcount()
  ```

- [ ] Option 2: HyperLogLog sketch (if approximate cardinality OK)
  ```cpp
  unordered_map<string, HyperLogLog> country_sketches;
  // Probabilistic cardinality (±5% error, negligible space)
  ```

- [ ] Option 3: Radix sort on hashes
  ```cpp
  vector<pair<string, uint64_t>> names_with_hash;
  // Sort by (country, hash), count runs of unique hashes
  ```

- [ ] **Choose Option 1 (fixed-width bitset)** — simplest, deterministic, exact answer

### Implement the Fix

- [ ] In `track-c/src/main.cpp`:
  - [ ] Include `<bitset>` and `<functional>`
  - [ ] Replace `unordered_set<string> names;` with `unordered_map<string, bitset<65536>> country_hashes;`
  - [ ] In the loop where you insert names:
    ```cpp
    for (auto& row : csv_rows) {
      string country = row.country;
      string name = row.customer_name;
      uint64_t hash = hash<string>()(name) & 0xFFFF;  // 16-bit hash
      country_hashes[country][hash] = 1;  // Set bit
    }
    ```
  - [ ] At output time, count distinct per country:
    ```cpp
    for (auto& [country, bits] : country_hashes) {
      int distinct = bits.count();
      output << country << " " << distinct << "\n";
    }
    ```

### Test Locally

- [ ] Compile Track C locally:
  ```bash
  cd D:\challenge\speed-demon-starter\track-c
  g++ -std=c++20 -O3 -DNDEBUG -static -s -o solution src/main.cpp
  ```

- [ ] Run with baseline data:
  ```bash
  python3 check.py c --data data/c --expected <SHA256_FROM_BASELINE>
  ```
  - [ ] Verify: **PASS** (byte-for-byte match)
  - [ ] Record execution time (should be <0.2s if bitset approach works)
  - [ ] Record memory usage (should be <100MB)

- [ ] If FAIL:
  - [ ] Debug: Print a few intermediate values to verify hash distribution
  - [ ] Check: Is output format identical? (whitespace, newlines, etc.)
  - [ ] Re-test with reference implementation to ensure baseline hash is correct

- [ ] If PASS but slow (>1s):
  - [ ] Profile: Which part is slow? (I/O, hashing, bitset operations?)
  - [ ] Try Option 2 (HyperLogLog) or Option 3 (radix sort) instead
  - [ ] Re-test

- [ ] If PASS and fast (<0.5s):
  - [ ] ✅ Ready to push (Phase 1)

---

## Phase 2: Parallelize Track A (Wire Threading)

### Analyze Current Code

- [ ] Open `track-a/src/main.cpp`
- [ ] Locate the line that reads input: `std::getline(stdin, line)` or file read
- [ ] Understand the processing: parse line → extract endpoint → add to hash map
- [ ] Estimate: how many lines? (3.1M log lines) and is it I/O or CPU bound?

### Design Parallel Version

- [ ] Study `track-a/src/parallel_ranges.hpp`:
  - [ ] What's the thread pool API? (`spawn()`, `queue()`, `wait()`?)
  - [ ] How do we launch tasks on the pool?
  - [ ] How do we collect results from threads?

- [ ] Design the parallel algorithm:
  1. Read entire file into memory (or mmap)
  2. Find N split points (12 for 12 threads)
  3. Launch thread per chunk: parse locally → build local hash map
  4. Main thread merges all local maps into final output
  5. Output final aggregated result

### Implement

- [ ] In `track-a/src/main.cpp`:
  - [ ] Read input into `std::string buffer` or use mmap
  - [ ] Create 12 tasks (one per chunk):
    ```cpp
    vector<future<unordered_map<string, uint16_t>>> futures;
    for (int i = 0; i < 12; i++) {
      futures.push_back(
        async(launch::async, [&, i]() {
          // Parse chunk i, return local map
          return parse_chunk(buffer, i);
        })
      );
    }
    ```
  - [ ] Merge results:
    ```cpp
    unordered_map<string, uint16_t> final_map;
    for (auto& f : futures) {
      auto local = f.get();
      for (auto& [key, val] : local) {
        final_map[key] += val;
      }
    }
    ```

- [ ] Or use `parallel_ranges.hpp` directly if available

### Test Locally

- [ ] Compile Track A:
  ```bash
  cd D:\challenge\speed-demon-starter\track-a
  g++ -std=c++20 -O3 -DNDEBUG -pthread -static -s -o solution src/main.cpp src/parallel_ranges.cpp
  ```

- [ ] Run with sample data:
  ```bash
  python3 check.py a --data data/a --expected <SHA256_FROM_BASELINE>
  ```
  - [ ] Verify: **PASS** (byte-for-byte match)
  - [ ] Record time (should be 0.1-0.3s if threading works; baseline is 0.628s)
  - [ ] Record memory (should be similar or slightly higher, ~50MB)

- [ ] If FAIL:
  - [ ] Debugging: Print sample output vs baseline to spot differences
  - [ ] Common issue: Sort order of results (if you're merging maps, ensure deterministic order)
  - [ ] Re-verify baseline hash is correct

- [ ] If PASS but slow or OOM:
  - [ ] Check: Are all 12 threads actually running? (Use `std::thread::hardware_concurrency()`)
  - [ ] Reduce thread count (4-6) and re-test
  - [ ] If memory explodes, reduce chunk size (fewer threads = smaller local maps)

- [ ] If PASS and fast:
  - [ ] ✅ Ready to push (Phase 2)

---

## Phase 3: Parallelize Track D (Dijkstra Pool)

### Analyze Current Code

- [ ] Open `track-d/src/main.cpp`
- [ ] Locate Dijkstra implementation
- [ ] Understand the query grouping: Are queries already grouped by source?
- [ ] Estimate: how many unique sources? (possible parallelism opportunity)

### Design Parallel Version

- [ ] For each unique source vertex:
  - [ ] Run one independent Dijkstra (finds all-shortest-paths from that source)
  - [ ] Launch 12 tasks in parallel (one per source)
  - [ ] Merge results into final distance array

### Implement

- [ ] Pseudocode:
  ```cpp
  vector<int> unique_sources = extract_unique_sources(queries);
  map<int, vector<int>> distances_by_source;
  
  // Parallelize over sources
  vector<future<>> tasks;
  for (int src : unique_sources) {
    tasks.push_back(async([&, src]() {
      distances_by_source[src] = dijkstra(src, graph);
    }));
  }
  for (auto& t : tasks) t.wait();
  
  // Output results
  for (auto& query : queries) {
    output << distances_by_source[query.src][query.dest] << "\n";
  }
  ```

### Test Locally

- [ ] Compile Track D:
  ```bash
  cd D:\challenge\speed-demon-starter\track-d
  g++ -std=c++20 -O3 -DNDEBUG -pthread -static -s -o solution src/main.cpp
  ```

- [ ] Run:
  ```bash
  python3 check.py d --data data/d --expected <SHA256_FROM_BASELINE>
  ```
  - [ ] Verify: **PASS**
  - [ ] Record time (baseline 8.748s → target <1s; expect 1.5-3s with parallelism)
  - [ ] Record memory

- [ ] If FAIL:
  - [ ] Likely cause: thread safety on shared graph structure
  - [ ] Ensure graph is read-only (no mutations during Dijkstra)
  - [ ] Check: Is the output distance ordering preserved? (same as reference)

- [ ] ✅ Ready to push (Phase 3)

---

## Phase 4: Parallelize Track B (Tokenize & Merge)

### Analyze Current Code

- [ ] Open `track-b/src/main.cpp`
- [ ] Understand the two-pass algorithm: read queries first, then index only those terms
- [ ] Identify the tokenization loop (where time is spent)

### Design Parallel Version

- [ ] Chunk the 253MB corpus into 12 regions
- [ ] Each thread tokenizes its region, builds local posting lists for query terms
- [ ] Main thread merges all posting lists and answers queries

### Test Locally

- [ ] Similar to Track A and D
- [ ] Run: `python3 check.py b --data data/b --expected <SHA256>`
- [ ] Verify: **PASS**
- [ ] ✅ Ready to push (Phase 4)

---

## Before Each Push to Board

### Final Verification Checklist

- [ ] **Correctness**: Run `check.py --expected <baseline_sha256>` one more time locally
- [ ] **Output file exists**: Confirm output file is at correct path (`/out/summary.txt`, etc.)
- [ ] **Program exits 0**: Check return code is 0, not error
- [ ] **No stdout/stderr**: Program should only write to `/out/` file, not stdout
- [ ] **Time under board cap**:
  - [ ] Track A: local time < 1.88s (board cap = 3x baseline 0.628s)
  - [ ] Track B: local time < 18.8s
  - [ ] Track C: local time < 10.1s
  - [ ] Track D: local time < 26.2s

### Commit Message Format

```bash
git add track-a/src/main.cpp track-a/.speed-demon.toml
git commit -m "Optimize: Parallelize Track A with thread pool

- Chunk input into 12 regions
- Per-thread hash table + parallel parse
- Merge results at end
- Expected: 0.628s → 0.15s (4.2x speedup)"

git push origin speed-demon
```

### After Push

- [ ] Wait 2-5 minutes for board evaluation
- [ ] Check https://speeddemon.dev/board/ for NewtonRedHeat results
- [ ] If scored: Copy Run ID from board
- [ ] Log Run ID in `track-a/PERF.md`:
  ```markdown
  | 1 | Parallelize Track A | run-xxxxx | 0.15 | 50 | 65.0 |
  ```
- [ ] Commit and push the PERF.md update

### If Board Shows FAILED

- [ ] Check board error message
- [ ] If "wrong output": Run `check.py` locally again to debug
- [ ] If "timeout": Your time exceeded board cap — optimize further or reduce threads
- [ ] Fix and re-push after fixing (next 5-min window)

---

## Summary: Sequence of Operations

1. ✅ **Preparation** (2-3h): Read code, understand APIs, baseline measurements
2. ✅ **Track C** (6h): Fix bitset + test locally
3. ✅ **Track A** (3h): Parallelize + test locally
4. ✅ **Track D** (3h): Parallelize Dijkstra + test locally
5. ✅ **Track B** (3h): Parallelize tokenize + test locally
6. ⏳ **Push Phase 1** (Track C only): Local verified PASS → Push → Board eval (5 min)
7. ⏳ **Push Phase 2** (Track A+D+B): Local verified PASS → Push → Board eval (5 min)
8. ⏳ **Iterate** (10-15h): Micro-tune based on board results, push refinements

---

## DO NOT PUSH UNLESS

- [ ] Local `check.py` shows **PASS** for that track
- [ ] SHA-256 output hash matches baseline
- [ ] Program exits with code 0
- [ ] No unexpected stdout/stderr
- [ ] Time is under board cap
- [ ] Commit message clearly describes the change

**Violating any of these = automatic 0 points for that track.**

---

**Ready to proceed? Answer "yes" only when ALL Preparation items above are checked. Then proceed Phase 1.** ✅
