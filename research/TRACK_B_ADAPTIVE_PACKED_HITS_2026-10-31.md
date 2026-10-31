# Track B: exact 32-bit adaptive worker-local hits (October 31)

**Synthetic-only research, not a leaderboard result.**

## New observation

The current `track-b/src/main.cpp` stores `Hit { uint32_t term, document; }` (8 bytes) for each distinct Boolean term/document membership. The full-public diagnostics in `track-b/PERF.md` report ~21.1 million such memberships, i.e. ~169 MB of flat hit payload alone, before the temporary 4-byte posting scatter and final postings.

On the published full seed-1 dataset there are 53,444 Boolean query terms and 250,000 documents. With 12 contiguous byte-range workers, each worker sees roughly 20,800 consecutive document IDs. Both the Boolean term index and the document offset from that worker's first document ID fit in 16 bits **for this public dataset**.

A 32-bit hit is `(term_index << 16) | (document_id - worker_first_document)`. Decode to absolute `uint32_t` document IDs when doing the existing prefix/scatter. **Do not assume these widths hold for other seeds.** Start in packed mode and switch once to an 8-byte `Hit` vector if `term_index >= 65536`, `doc < base`, or `doc - base >= 65536`; convert all earlier packed hits during the transition. This is an exact, seed-independent fallback.

## Independent local validation

- 800 randomized rounds of 500 hits each under AddressSanitizer and UndefinedBehaviorSanitizer. Cases included term overflow, document offset overflow, and an ID below shard base. Expansion matched a separate vector-of-struct oracle exactly in every case.
- Synthetic one-shard count/prefix/scatter kernel: 1,750,056 memberships over 53,444 terms and 20,834 documents. A 64-bit checksum of every term/document pair agreed between packed and flat.
- Two alternating measurements, **isolated kernel only**:

| Mode | Append (ms) | Prefix/scatter (ms) | Process peak RSS |
|---|---:|---:|---:|
| Flat, first | 12.3 | 8.6 | 22,400 KiB |
| Packed, first | 9.6 | 5.7 | 15,488 KiB |
| Flat, second | 12.5 | 7.0 | 22,400 KiB |
| Packed, second | 10.3 | 8.9 | 15,488 KiB |

Memory fell 30.9% in this kernel. Timing results were variable; **no proven production speedup**. On the real ~21.1M hit payload, packing could save ~84 MB of live payload, but actual process peak depends on allocation overlap. The current best B board run used 300,158,976 bytes peak.

## Next experiment

Build a separate production candidate that preserves all existing query/phrase logic and changes only the local hit representation. Compare with the current `425051d` submission using official SHA-256 and complete sandbox wall time/RSS, then test against the independent oracle on seeds that force both wide fallbacks. Compare this candidate with the adaptive hot-term bitmap approach rather than combining them blindly.

The scoring branch was **not changed** because this runtime cannot download the official 254 MB B corpus or run the full-output SHA check. No new score is claimed.
