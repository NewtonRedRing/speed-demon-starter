/* Track B: exact 12-byte key lookup on the production 16-byte term table.
 * C candidates are compiled independently with GCC and Clang and linked into
 * the unchanged Rust indexing/evaluation engine for an A/B/C comparison.
 *
 * Slots are 16 bytes: 12 normalized key bytes plus the native-endian u32
 * one-based ID (zero means empty). Lookup performs exactly the same linear
 * probing and 12-byte equality as Terms::find in main.rs.
 */
#include <immintrin.h>
#include <stddef.h>
#include <stdint.h>

uint32_t track_b_find_c(const void *table, size_t mask,
                        const void *key_bytes, size_t hash, uint32_t n) {
    const __m128i *slots = (const __m128i *)table;
    const __m128i key = _mm_loadu_si128((const __m128i *)key_bytes);
    size_t i = hash & mask;
    for (;;) {
        const __m128i slot = _mm_loadu_si128(slots + i);
        const uint32_t id = (uint32_t)_mm_extract_epi32(slot, 3);
        const unsigned matched = (unsigned)_mm_movemask_epi8(_mm_cmpeq_epi8(slot, key));
        if ((matched & 0x0FFFu) == 0x0FFFu || id == 0)
            return id == 0 ? n : id - 1;
        i = (i + 1) & mask;
    }
}
