# Track B: exact fused token hashing (October 31, 2026)

**Status: validated isolated C++ prototype, NOT a production or official-board improvement.** The `speed-demon` submission branch is unchanged.

## New finding

The current `track-b/src/main.cpp` scanner copies each token into a 32-byte buffer and subsequently calls `std::hash<std::string_view>` on the copied bytes. This means each token is read twice: once while identifying ASCII token boundaries and again while hashing. Its `Words` dictionary also uses `std::hash` at construction time.

An exact fused alternative updates a 64-bit FNV-1a hash **while tokenizing**. Query words use the same FNV hash when building the dictionary. The dictionary retains exact byte-string comparison on a hash/fingerprint hit, so collisions cannot create false matches. The token buffer is still required for exact verification. No change to phrase automaton, Boolean semantics or postings is necessary.

For each valid ASCII-lowercase byte `c`:

```cpp
constexpr uint64_t offset = 14695981039346656037ULL;
constexpr uint64_t prime  = 1099511628211ULL;
uint64_t h = offset;
// During the existing token scanner, after ASCII-lowercasing c:
h ^= c;
h *= prime;
// On token completion: words.find({token, size}, h)
```

In `Words`, replace both query-dictionary `std::hash` and token-lookup `std::hash` with this shared hash. Preserve exact `terms_[id].text == text` on fingerprint matches, and preserve the existing `size > 32` rejection. No hash-only acceptance is permitted.

## Measured local synthetic test

C++20, 50,000 synthetic query terms, 5,000,000 token instances, 25% query-word hits, remaining words random, length 4–12; identical checksum `31326771344` for both implementations.

| Compiler / variant | Three timings (ms) | Median |
|---|---|---:|
| GCC -O3 -march=native; existing copy+std::hash | 295.99, 281.47, 284.82 | 284.82 |
| GCC -O3 -march=native; fused FNV-1a | 215.86, 203.20, 203.74 | 203.74 |
| Clang -O3 -march=x86-64-v3; existing | 291.74, 318.92, 290.45 | 291.74 |
| Clang -O3 -march=x86-64-v3; fused | 213.05, 220.33, 231.13 | 220.33 |

GCC isolated kernel speedup ~1.40x, Clang ~1.32x. These numbers include dictionary probing and scanning, but exclude the real 254MB corpus, document parsing, phrase automaton, posting construction, query evaluation, output and scoring CPU constraints. **They do not imply a whole-track speedup.**

## Independent exactness oracle

An additional independent C++20 oracle compares fused FNV tokenization and lookup with a separate reference ASCII-lowercase tokenizer and `std::unordered_map<std::string, u32>` lookup. It passed **800 randomized corpora** with 400 candidate tokens each (320,000 generated candidates), using mixed case, punctuation, high UTF-8 bytes, and lengths 1–50 including the 32-byte cutoff. Built with `-Wall -Wextra -Werror -fsanitize=address,undefined` and passed.

This proves the prototype's word-ID lookup behavior on the randomized tests, not the production full SHA-256.

## Important rejected alternative

We also benchmarked **in-place 32-bit packed hit grouping** versus the current count/scatter model. On 3,448,805 synthetic memberships, peak RSS dropped from 48,512 KiB to 25,088 KiB, but count/group/sort time increased from ~53–60ms to ~361–394ms. Even without the necessary per-term sorting, in-place grouping was about twice as slow as stable scatter. **Reject this alternative** despite the memory benefit; it is unlikely to improve B's combined machine score.

## Next implementation and gate

1. On a non-submission candidate branch, patch only `Words` and `scan` to use the fused hash, then compile the entire production `track-b/src/main.cpp`.
2. Run official seed-1 `docs.txt` and `queries.txt`, compare `results.txt` against the official SHA-256 and the current `425051d` output; run multiple holdout-like seeds or independent reference cases.
3. Measure whole-track time, peak RSS, and source size on the same machine; push to `speed-demon` **only if non-regressing**. Preserve rollback commit `425051dc91a0713b1c4e97e57308221a3df71dff`.

The full public corpus is linked on https://speeddemon.dev/board/ but was not accessible in this isolated execution container. GitHub Actions previously failed before running any steps, so no full official validation occurred in this run.

## Board context

Live feed at 2026-10-31T05:40:35Z: NewtonRedHeat B best **53.122178**, latest **53.024437** (run `run-6c7727319a49`), target **69.556**. Another team's publicly reported best B run demonstrates that ~0.255s and ~41.7MB is attainable, compared with our ~0.538s and ~300MB; that observation does not reveal its implementation.

Reference: https://s3.amazonaws.com/raptors.dev/speeddemon.dev/2026/board/board.json
