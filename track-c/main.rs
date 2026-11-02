//! Track C — Join. Parses a hostile `records.csv` (quoted commas, `""`
//! escapes, LFs inside quoted notes, mixed CRLF/LF, BOMs at the start of
//! some records), joins each record's `region_id` to a country through
//! `regions.csv`, and writes one line of aggregates per country.
//!
//! Spec: real-spec/speed-demon-2026/specs/c-join.md. Design notes:
//! - Region -> country is a dense array indexed by `region_id`, so the
//!   join is one bounds-checked array read per record.
//! - Amounts are integer cents end to end; nothing touches a float.
//! - A record is parsed straight from the mapped bytes: digit runs with a
//!   branch-free SWAR conversion, quoted fields with an AVX2 search for the
//!   closing quote (a `"` followed by another `"` is an escaped pair).
//! - `names_distinct` keeps nothing per record. All workers share one
//!   lock-free table that numbers each distinct name (by a 64-bit hash of
//!   its length and boundary bytes; escaping is one to one, so equal names have equal raw
//!   bytes) and one bit per (country, name number). A country's distinct
//!   count is the popcount of its row. Lookups are batched and prefetched
//!   so their cache misses overlap.
//! - The file is parsed in parallel, in many more chunks than threads that
//!   workers claim from a counter (cores differ in speed). A chunk cut is
//!   the first LF that is followed by a digit or a BOM, which spec 2.2
//!   makes a record boundary: names are letters and notes lowercase words.
//! - The last few KB are copied behind zero padding so SIMD loads never
//!   run past the file-backed mapping.

use std::arch::x86_64::{
    __m256i, _bzhi_u64, _mm_clmulepi64_si128, _mm_crc32_u64, _mm_cvtsi128_si64, _mm_cvtsi64_si128,
    _mm_set1_epi8, _mm256_cmpeq_epi8, _mm256_loadu_si256, _mm256_movemask_epi8,
    _mm256_set1_epi8,
};
use std::cell::UnsafeCell;
use std::sync::atomic::{AtomicU32, AtomicU64, AtomicU8, AtomicUsize, Ordering::{Acquire, Relaxed, Release}};
use std::sync::Mutex;

/// Maps `path` read-only for the life of the process. The harness
/// pre-warms `/data` into the page cache before the run, so those pages
/// are charged outside this run's cgroup and mapping them adds nothing to
/// peak memory; `fs::read` would copy the whole file into charged heap.
// The board always builds and runs this inside the pinned Linux images
// (track-c/Dockerfile), so there is no non-unix target to support.
fn map_file(path: &str) -> std::io::Result<&'static [u8]> {
    use std::os::fd::AsRawFd;
    extern "C" {
        fn mmap(addr: *mut u8, len: usize, prot: i32, flags: i32, fd: i32, off: i64) -> *mut u8;
    }
    let file = std::fs::File::open(path)?;
    let len = file.metadata()?.len() as usize;
    if len == 0 {
        return Ok(&[]);
    }
    // PROT_READ = 1, MAP_PRIVATE = 2.
    let p = unsafe { mmap(std::ptr::null_mut(), len, 1, 2, file.as_raw_fd(), 0) };
    if p as isize == -1 {
        return Err(std::io::Error::last_os_error());
    }
    Ok(unsafe { std::slice::from_raw_parts(p, len) })
}

// --- Regions ------------------------------------------------------------

/// Countries in first-seen order, and `region_id -> group + 1` (0 means
/// "no such region"). Spec 2.4 bounds the full/sample dataset to 240
/// countries and tiny to 24, always well under 254, so group+1 always
/// fits one byte with no overflow scheme needed. Every thread fills its own
/// slice of the file, so both tables are atomic bytes and the country
/// list sits behind a lock that is taken once per country.
struct Regions {
    countries: Mutex<Vec<[u8; 3]>>,
    index: Vec<AtomicU8>,
    group_of: Vec<AtomicU8>,
}

impl Regions {
    fn new(data: &[u8]) -> Regions {
        // Ids ascend, so the last line has the largest; size the table once.
        let last = data.rsplit(|&b| b == b'\n').nth(1).unwrap_or(&[]);
        let max_id = last
            .iter()
            .take_while(|d| d.is_ascii_digit())
            .fold(0usize, |a, &d| a * 10 + (d - b'0') as usize);
        Regions {
            countries: Mutex::new(Vec::new()),
            // Spec 2.3: a code is three bytes `A`-`Z`, so five bits each identify it.
            index: zeroed(1 << 15),
            group_of: zeroed(max_id + 1),
        }
    }

    /// Reads the lines that start in `data[from..to)`.
    fn load(&self, data: &[u8], from: usize, to: usize) {
        let mut at = from;
        while at < to {
            let mut id = 0usize;
            if at + 8 <= data.len() {
                // Branch-free digit run: the id's length varies, so a byte loop mispredicts.
                let (v, n) = digits(data, at);
                id = v as usize;
                at += n;
            }
            while data[at] != b',' {
                id = id * 10 + (data[at] - b'0') as usize;
                at += 1;
            }
            let code = &data[at + 1..at + 4];
            // Packs the three letters into 15 bits (5 per letter) to index a dense table.
            let slot = &self.index[code.iter().fold(0usize, |a, &c| a << 5 | (c & 31) as usize)];
            let mut group = slot.load(Relaxed);
            if group == 0 {
                let mut countries = self.countries.lock().unwrap();
                group = slot.load(Relaxed);
                if group == 0 {
                    countries.push([code[0], code[1], code[2]]);
                    group = countries.len() as u8;
                    slot.store(group, Relaxed);
                }
            }
            self.group_of[id].store(group, Relaxed);
            at += 4;
            // The common line ends right after the three-letter code.
            if data[at] == b'\n' {
                at += 1;
            } else {
                while data[at] != b'\n' {
                    at += 1;
                }
                at += 1;
            }
        }
    }
}

/// Start of the first line that begins after `from`.
fn next_line(data: &[u8], from: usize) -> usize {
    data[from..].iter().position(|&b| b == b'\n').map_or(data.len(), |p| from + p + 1)
}

// --- Aggregation --------------------------------------------------------

#[derive(Clone, Default)]
struct Agg {
    count: u64,
    id_sum: u64,
    amount_sum: i64,
    amount_min: i64,
    amount_max: i64,
    name_bytes: u64,
    note_bytes: u64,
    names_distinct: u64,
}

impl Agg {
    fn new() -> Agg {
        Agg {
            amount_min: i64::MAX,
            amount_max: i64::MIN,
            ..Agg::default()
        }
    }
}

// --- Distinct names -----------------------------------------------------

/// Exact shared name table. The hot slot is exactly 16 bytes. The first
/// word is also the publication atomic: zero=free, all-ones=being filled,
/// high bit set=ready. Generated name bytes are printable ASCII, so that high
/// bit cannot collide with real key data. Names up to 16 raw bytes are stored
/// exactly in the slot. Longer names store their first eight bytes in the first
/// word and a mark, their length and an arena offset in the second; bytes eight
/// onwards are compared from the side arena before equality is accepted.
const NAME_SLOTS: usize = 1 << 17;
const NAME_WORDS: usize = NAME_SLOTS / 64;
// One row per country plus `_UNMATCHED`; unused rows are never touched.
const MAX_GROUPS: usize = 256;
const NAME_ARENA: usize = 1 << 20;
const OCCUPIED: u64 = 1 << 63;
const BUSY: u64 = u64::MAX;
const LONG: u64 = 1 << 63; // Long-name tag distinguishes a 16-byte prefix.

#[repr(C, align(16))]
#[derive(Default)]
struct NameSlot {
    lo: AtomicU64,
    hi: UnsafeCell<u64>,
}
unsafe impl Sync for NameSlot {}

/// Four slots on one cache line, probed together.
#[repr(C, align(64))]
#[derive(Default)]
struct Bucket([NameSlot; 4]);

struct ByteArena(UnsafeCell<Vec<u8>>);
unsafe impl Sync for ByteArena {}

struct Names {
    buckets: Vec<Bucket>,
    arena: ByteArena,
    used: AtomicU32,
    bits: Vec<AtomicU64>,
}

fn zeroed<T>(n: usize) -> Vec<T> {
    // Zeroed pages are only charged once they are touched. T is an atomic integer.
    let layout = std::alloc::Layout::array::<T>(n).unwrap();
    unsafe { Vec::from_raw_parts(std::alloc::alloc_zeroed(layout).cast(), n, n) }
}

#[inline(always)]
fn byte_mask(n: usize) -> u64 {
    // The low n bytes (all of them from 8 up); BZHI keeps every bit for an index above 63.
    unsafe { _bzhi_u64(!0, 8 * n.min(8) as u32) }
}

#[inline(always)]
unsafe fn name_words(p: usize, len: usize) -> (u64, u64, u64) {
    let q = p as *const u8;
    let lo = q.cast::<u64>().read_unaligned() & byte_mask(len);
    let hi = q.add(8).cast::<u64>().read_unaligned() & byte_mask(len.saturating_sub(8));
    // The slot's second word matches under the mask; a long name leaves its offset out.
    if len > 16 { (lo, LONG | (len as u64) << 32, !0 << 32) } else { (lo, hi, !0) }
}

impl Names {
    fn new() -> Self {
        Self {
            buckets: (0..NAME_SLOTS / 4).map(|_| Bucket::default()).collect(),
            arena: ByteArena(UnsafeCell::new(vec![0u8; NAME_ARENA])),
            used: AtomicU32::new(0),
            bits: zeroed(MAX_GROUPS * NAME_WORDS),
        }
    }

    #[inline(always)]
    fn long_same(&self, off: u64, p: usize, len: usize) -> bool {
        let off = off as u32 as usize;
        let tail = len - 8;
        let arena = unsafe { &*self.arena.0.get() };
        &arena[off..off + tail]
            == unsafe { std::slice::from_raw_parts((p as *const u8).add(8), tail) }
    }

    #[inline(always)]
    fn slot(&self, i: usize) -> &NameSlot {
        &self.buckets[i / 4].0[i % 4]
    }

    /// The slot already holding this name, if it is in the home bucket (`home` is
    /// the bucket's first slot): two vector compares of the whole bucket, no branch
    /// per slot. A slot matches when both of its words do. A slot another thread is
    /// still filling can only fail to match, which sends the caller to the slow path.
    #[inline(always)]
    fn probe(&self, home: usize, ready_lo: u64, key_hi: u64, mask: u64) -> Option<usize> {
        use std::arch::x86_64::{
            _mm256_and_si256, _mm256_castsi256_pd, _mm256_cmpeq_epi64, _mm256_movemask_pd,
            _mm256_set_epi64x,
        };
        unsafe {
            let at = self.slot(home) as *const NameSlot as *const __m256i;
            let key = _mm256_set_epi64x(key_hi as i64, ready_lo as i64, key_hi as i64, ready_lo as i64);
            let keep = _mm256_set_epi64x(mask as i64, -1, mask as i64, -1);
            let eq = |v: __m256i| {
                let hit = _mm256_cmpeq_epi64(_mm256_and_si256(v, keep), key);
                _mm256_movemask_pd(_mm256_castsi256_pd(hit)) as usize
            };
            let m = eq(_mm256_loadu_si256(at)) | eq(_mm256_loadu_si256(at.add(1))) << 4;
            let hit = m & m >> 1 & 0b0101_0101;
            (hit != 0).then(|| home + hit.trailing_zeros() as usize / 2)
        }
    }

    #[inline(always)]
    fn number(&self, h: u64, p: usize, len: usize) -> usize {
        let (raw_lo, key_hi, mask) = unsafe { name_words(p, len) };
        let ready_lo = raw_lo | OCCUPIED;
        let home = h as usize & (NAME_SLOTS - 1) & !3;
        if let Some(i) = self.probe(home, ready_lo, key_hi, mask) {
            if len <= 16 || self.long_same(unsafe { *self.slot(i).hi.get() }, p, len) {
                return i;
            }
        }
        let mut i = home;
        loop {
            let mut lo = self.slot(i).lo.load(Acquire);
            while lo == BUSY {
                std::hint::spin_loop();
                lo = self.slot(i).lo.load(Acquire);
            }
            if lo == 0 {
                if self.slot(i).lo.compare_exchange(0, BUSY, Acquire, Acquire).is_ok() {
                    let mut hi = key_hi;
                    if len > 16 {
                        let tail = len - 8;
                        let off = self.used.fetch_add(tail as u32, Relaxed) as usize;
                        assert!(off + tail <= NAME_ARENA, "name arena exhausted");
                        hi |= off as u64;
                        unsafe {
                            std::ptr::copy_nonoverlapping(
                                (p as *const u8).add(8),
                                (&mut *self.arena.0.get()).as_mut_ptr().add(off),
                                tail,
                            );
                        }
                    }
                    unsafe { *self.slot(i).hi.get() = hi };
                    self.slot(i).lo.store(ready_lo, Release);
                    return i;
                }
                continue;
            }
            if (lo & !OCCUPIED) == raw_lo {
                let hi = unsafe { *self.slot(i).hi.get() };
                if hi & mask == key_hi && (len <= 16 || self.long_same(hi, p, len)) {
                    return i;
                }
            }
            i = (i + 1) & (NAME_SLOTS - 1);
        }
    }

    fn distinct(&self, group: usize) -> u64 {
        self.bits[group * NAME_WORDS..(group + 1) * NAME_WORDS]
            .iter().map(|w| w.load(Relaxed).count_ones() as u64).sum()
    }
}

const BATCH: usize = 8;

struct Sightings<'a> {
    names: &'a Names,
    seen: [(usize, u64, usize, usize); BATCH],
    seen_len: usize,
    bits: [(usize, u64); BATCH],
    bits_len: usize,
}

impl<'a> Sightings<'a> {
    fn new(names: &'a Names) -> Self {
        Sightings {
            names,
            seen: [(0, 0, 0, 0); BATCH],
            seen_len: 0,
            bits: [(0, 0); BATCH],
            bits_len: 0,
        }
    }

    #[inline]
    fn push(&mut self, group: usize, h: u64, p: usize, len: usize) {
        let home = h as usize & (NAME_SLOTS - 1);
        prefetch_read(&self.names.buckets[home / 4] as *const Bucket);
        self.seen[self.seen_len] = (group, h, p, len);
        self.seen_len += 1;
        if self.seen_len == BATCH {
            self.flush();
        }
    }

    fn flush(&mut self) {
        for &(word, bit) in &self.bits[..self.bits_len] {
            self.names.bits[word].fetch_or(bit, Relaxed);
        }
        self.bits_len = 0;
        for &(group, h, p, len) in &self.seen[..self.seen_len] {
            let id = self.names.number(h, p, len);
            let word = group * NAME_WORDS + id / 64;
            prefetch_write::<AtomicU64>(&self.names.bits[word]);
            self.bits[self.bits_len] = (word, 1 << (id % 64));
            self.bits_len += 1;
        }
        self.seen_len = 0;
    }

    fn finish(mut self) {
        self.flush();
        self.flush();
    }
}

/// Gives the whole pages inside b back to the kernel, so that each worker does
/// its share of the unmapping that exit would otherwise do alone. Only for
/// memory that is finished with.
fn release(b: &[u8]) {
    extern "C" {
        fn madvise(addr: *mut u8, len: usize, advice: i32) -> i32;
    }
    let (p, n) = (b.as_ptr() as usize, b.len());
    let (lo, hi) = ((p + 4095) & !4095, (p + n) & !4095);
    if hi > lo {
        unsafe { madvise(lo as *mut u8, hi - lo, 4) }; // MADV_DONTNEED
    }
}

/// Hints that `p` will be read soon; never faults, even past a mapping's end.
#[inline(always)]
fn prefetch_read<T>(p: *const T) {
    unsafe { std::arch::x86_64::_mm_prefetch::<{ std::arch::x86_64::_MM_HINT_T0 }>(p.cast()) };
}

/// Hints that `p` will be written soon; never faults.
#[inline(always)]
fn prefetch_write<T>(p: *const T) {
    unsafe { std::arch::asm!("prefetchw [{0}]", in(reg) p, options(nostack, preserves_flags)) };
}

#[inline(always)]
fn hash_name(data: &[u8], from: usize, to: usize) -> u64 {
    let len = to - from;
    unsafe {
        let word = |p: usize| data.as_ptr().add(p).cast::<u64>().read_unaligned();
        let first = word(from) & byte_mask(len);
        let last = word(to - 8) * (len >= 8) as u64;
        let h = _mm_crc32_u64(_mm_crc32_u64(len as u64, first), last);
        h.wrapping_mul(0x9E37_79B9_7F4A_7C15) ^ h.rotate_left(29)
    }
}

// --- Record parsing (spec section 2.1) ----------------------------------

/// The run of ASCII digits at `data[at..]` as (value, length).
#[inline]
fn digits(data: &[u8], at: usize) -> (u64, usize) {
    let w = u64::from_le_bytes(data[at..at + 8].try_into().unwrap());
    // Digit bytes have high nibble 3, so a nonzero byte here is a non-digit. The first one ends the run.
    let not_digit = (w & 0xF0F0_F0F0_F0F0_F0F0) ^ 0x3030_3030_3030_3030;
    let mut n = not_digit.trailing_zeros() as usize / 8;
    let mut v = fold8(w << (64 - 8 * n.max(1)));
    while n >= 8 && data[at + n].is_ascii_digit() {
        v = v * 10 + (data[at + n] - b'0') as u64;
        n += 1;
    }
    (v, n)
}

/// Eight digit bytes, most significant first, as a number. SWAR: fold pairs,
/// quads and octets with multiplies.
#[inline(always)]
fn fold8(w: u64) -> u64 {
    let w = (w & 0x0F0F_0F0F_0F0F_0F0F).wrapping_mul(2561) >> 8;
    let w = ((w & 0x00FF_00FF_00FF_00FF).wrapping_mul(6_553_601)) >> 16;
    ((w & 0x0000_FFFF_0000_FFFF).wrapping_mul(42_949_672_960_001)) >> 32
}

// --- FALCON-C: tile-wide SIMD structural scanner -------------------------
// One pass over consecutive 64-byte tiles, rather than loading 128 bytes
// again for every ~61-byte record. Quote parity carries between tiles.
// Separators and quote counts carry across arbitrary-length quoted notes.
#[inline(always)]
fn prefix_xor(q: u64) -> u64 {
    unsafe {
        let p = _mm_clmulepi64_si128::<0>(_mm_cvtsi64_si128(q as i64), _mm_set1_epi8(-1));
        _mm_cvtsi128_si64(p) as u64
    }
}

struct Separators<'a> {
    data: &'a [u8],
    base: usize,
    marks: u64,
    quote_mask: u64,
    quotes_before: u32,
    in_quotes: bool,
    loaded: bool,
}

impl<'a> Separators<'a> {
    #[inline(always)]
    fn new(data: &'a [u8], start: usize) -> Self {
        Self {
            data,
            base: start,
            marks: 0,
            quote_mask: 0,
            quotes_before: 0,
            in_quotes: false, // every claimed chunk begins at a record boundary
            loaded: false,
        }
    }

    /// Return the next delimiter outside quoted strings, and the number of
    /// quote characters preceding it (including all earlier tiles).
    #[inline(always)]
    fn next<const QUOTE_COUNT: bool>(&mut self) -> (usize, u32) {
        loop {
            if self.marks != 0 {
                let bit = self.marks.trailing_zeros();
                self.marks &= self.marks - 1;
                let count = if QUOTE_COUNT {
                    self.quotes_before + unsafe { _bzhi_u64(self.quote_mask, bit) }.count_ones()
                } else { 0 };
                return (self.base + bit as usize, count);
            }
            if self.loaded {
                self.quotes_before += self.quote_mask.count_ones();
                self.base += 64;
            } else {
                self.loaded = true;
            }
            assert!(self.base < self.data.len(), "truncated CSV record");
            let (quotes, commas, lines) = if self.base + 64 <= self.data.len() {
                unsafe {
                    let p = self.data.as_ptr().add(self.base).cast::<__m256i>();
                    let v0 = _mm256_loadu_si256(p);
                    let v1 = _mm256_loadu_si256(p.add(1));
                    let mask = |c: u8| {
                        let needle = _mm256_set1_epi8(c as i8);
                        let m0 = _mm256_movemask_epi8(_mm256_cmpeq_epi8(v0, needle)) as u32 as u64;
                        let m1 = _mm256_movemask_epi8(_mm256_cmpeq_epi8(v1, needle)) as u32 as u64;
                        m0 | (m1 << 32)
                    };
                    (mask(b'"'), mask(b','), mask(b'\n'))
                }
            } else {
                // Padded tail and tiny inputs: never issue a SIMD load past EOF.
                let mut q = 0u64;
                let mut c = 0u64;
                let mut n = 0u64;
                for (i, &v) in self.data[self.base..].iter().enumerate() {
                    q |= ((v == b'"') as u64) << i;
                    c |= ((v == b',') as u64) << i;
                    n |= ((v == b'\n') as u64) << i;
                }
                (q, c, n)
            };
            let inside = prefix_xor(quotes) ^ 0u64.wrapping_sub(self.in_quotes as u64);
            self.marks = (commas | lines) & !inside;
            self.quote_mask = quotes;
            self.in_quotes ^= (quotes.count_ones() & 1) != 0;
        }
    }
}

/// The n-digit number immediately preceding data[end], for 1 <= n <= 8.
#[inline(always)]
fn run(p: *const u8, end: usize, n: usize) -> u64 {
    let w = unsafe { p.wrapping_add(end).wrapping_sub(8).cast::<u64>().read_unaligned() };
    fold8(w & (!0 << (64 - 8 * n)))
}

/// Fused parse and aggregation; no per-record allocations, no full CSV rows.
fn parse_records(
    data: &[u8],
    start: usize,
    end: usize,
    regions: &Regions,
    sightings: &mut Sightings,
    groups: &mut [Agg],
) {
    let mut separators = Separators::new(data, start);
    // The numeric fields contain no quotes: the count at the previous
    // note delimiter is also the count at this record's first comma.
    let mut quotes_before_name = 0u32;
    let p = data.as_ptr();
    let unmatched = groups.len() - 1;
    let mut i = start;
    while i < end {
        prefetch_read(p.wrapping_add(i + 1536));
        let (d0, _) = separators.next::<false>();
        let (d1, q1) = separators.next::<true>();
        let (d2, q2) = separators.next::<true>();
        let (d3, _) = separators.next::<false>();
        let (lf, _) = separators.next::<false>();
        let skip_bom = 3 * (data[i] == 0xEF) as usize;
        let id = run(p, d0, d0 - i - skip_bom);
        let region = run(p, d3, d3 - d2 - 1) as usize;
        let name_quotes = (q1 - quotes_before_name) as usize;
        let note_quotes = (q2 - q1) as usize;
        quotes_before_name = q2;
        let name_raw = d1 - d0 - 1;
        let note_raw = d2 - d1 - 1;
        let name_bytes = name_raw - name_quotes / 2 - (name_quotes != 0) as usize;
        let note_bytes = note_raw - note_quotes / 2 - (note_quotes != 0) as usize;
        let ns = d0 + 1 + (name_quotes != 0) as usize;
        let ne = d1 - (name_quotes != 0) as usize;

        // Exact integer cents; the final two digits follow the decimal dot.
        let e = lf - (data[lf - 1] == b'\r') as usize;
        let negative = (data[d3 + 1] == b'-') as usize;
        let k = e - d3 - 4 - negative;
        let w = unsafe { p.wrapping_add(e).wrapping_sub(8).cast::<u64>().read_unaligned() };
        let w = ((w & 0xFF_FFFF_FFFF) & (!0 << (8 * (5 - k))))
            | (w >> 8 & (0xFFFF << 40));
        let cents = fold8(w << 8) as i64;
        let cents = if negative != 0 { -cents } else { cents };
        i = lf + 1;

        let g = if region < regions.group_of.len() {
            let x = unsafe { regions.group_of.get_unchecked(region) }.load(Relaxed);
            if x != 0 { x as usize - 1 } else { unmatched }
        } else {
            unmatched
        };
        let agg = &mut groups[g];
        agg.count += 1;
        agg.id_sum += id;
        agg.amount_sum += cents;
        agg.amount_min = agg.amount_min.min(cents);
        agg.amount_max = agg.amount_max.max(cents);
        agg.name_bytes += name_bytes as u64;
        agg.note_bytes += note_bytes as u64;
        sightings.push(g, hash_name(data, ns, ne), p.wrapping_add(ns) as usize, ne - ns);
    }
}

/// First record boundary after `from`: the position after an LF that is
/// followed by a digit (next id) or a BOM. Spec 2.2 makes names letters and
/// notes lowercase words, so an LF inside a quoted field is followed by a
/// letter or a quote.
fn cut(data: &[u8], from: usize) -> usize {
    let mut i = from;
    while i + 1 < data.len()
        && !(data[i] == b'\n' && (data[i + 1].is_ascii_digit() || data[i + 1] == 0xEF))
    {
        i += 1;
    }
    (i + 1).min(data.len())
}

/// The final bytes are copied behind padding and parsed last, so vector
/// lookahead cannot run beyond the file mapping.
const TAIL: usize = 8192;
const CHUNKS_PER_THREAD: usize = 64;

fn scan(
    data: &[u8],
    body: usize,
    region_data: &[u8],
    regions: &Regions,
    threads: usize,
    names: &Names,
) -> Vec<Agg> {
    let tail_at = if data.len() - body > 2 * TAIL {
        cut(data, data.len() - TAIL)
    } else {
        body
    };
    let chunks = threads * CHUNKS_PER_THREAD;
    let bound = |k: usize| match k {
        0 => body,
        k if k == chunks => tail_at,
        k => cut(data, body + (tail_at - body) * k / chunks).min(tail_at),
    };
    // Every thread, the main one included, loads a slice of regions.csv first.
    let first = next_line(region_data, 0);
    let line_bound = |k: usize| match k {
        0 => first,
        k if k > threads => region_data.len(),
        k => next_line(region_data, first + (region_data.len() - first) * k / (threads + 1)),
    };
    let next = AtomicUsize::new(0);
    // Everyone meets here once the regions are loaded, and again once every name is
    // in the table, when the workers count the matrix rows (a serial popcount of it
    // took ~2.5 ms).
    let barrier = std::sync::Barrier::new(threads + 1);
    let n_groups = || regions.countries.lock().unwrap().len() + 1;
    let parts: Vec<Vec<Agg>> = std::thread::scope(|s| {
        let handles: Vec<_> = (0..threads)
            .map(|t| {
                let (next, barrier) = (&next, &barrier);
                s.spawn(move || {
                    regions.load(region_data, line_bound(t), line_bound(t + 1));
                    barrier.wait();
                    let n_groups = n_groups();
                    let mut groups = vec![Agg::new(); n_groups];
                    let mut sightings = Sightings::new(names);
                    loop {
                        let k = next.fetch_add(1, Relaxed);
                        if k >= chunks {
                            break;
                        }
                        let (from, to) = (bound(k), bound(k + 1));
                        parse_records(data, from, to, regions, &mut sightings, &mut groups);
                        release(&data[from..to]);
                    }
                    sightings.finish();
                    barrier.wait();
                    for g in (t..n_groups).step_by(threads) {
                        groups[g].names_distinct = names.distinct(g);
                    }
                    groups
                })
            })
            .collect();
        regions.load(region_data, line_bound(threads), line_bound(threads + 1));
        let mut tail = vec![0u8; 16];
        tail.extend_from_slice(&data[tail_at..]);
        tail.resize(tail.len() + 128, 0);
        barrier.wait();
        let mut groups = vec![Agg::new(); n_groups()];
        let mut sightings = Sightings::new(names);
        parse_records(&tail, 16, 16 + data.len() - tail_at, regions, &mut sightings, &mut groups);
        sightings.finish();
        barrier.wait();
        let mut parts: Vec<Vec<Agg>> = handles
            .into_iter()
            .map(|h| h.join().expect("parse worker panicked"))
            .collect();
        parts.push(groups);
        parts
    });

    let mut out = vec![Agg::new(); n_groups()];
    for part in &parts {
        for (dst, src) in out.iter_mut().zip(part) {
            dst.names_distinct += src.names_distinct;
            if src.count == 0 {
                continue;
            }
            dst.count += src.count;
            dst.id_sum += src.id_sum;
            dst.amount_sum += src.amount_sum;
            dst.amount_min = dst.amount_min.min(src.amount_min);
            dst.amount_max = dst.amount_max.max(src.amount_max);
            dst.name_bytes += src.name_bytes;
            dst.note_bytes += src.note_bytes;
        }
    }
    out
}

fn aggregate(data: &[u8], region_data: &[u8], regions: &Regions, threads: usize) -> Vec<Agg> {
    let body = next_line(data, 0);
    if body >= data.len() { return vec![Agg::new()]; }
    let names = Names::new();
    let out = scan(data, body, region_data, regions, threads.max(1), &names);
    std::mem::forget(names);
    out
}

// --- Output (spec section 4) --------------------------------------------

/// Integer cents as `-?<int>.<2 digits>`; zero is `0.00`, never `-0.00`.
fn cents(c: i64) -> String {
    format!(
        "{}{}.{:02}",
        if c < 0 { "-" } else { "" },
        c.abs() / 100,
        c.abs() % 100
    )
}

fn write_line(out: &mut Vec<u8>, key: &[u8], a: &Agg) {
    out.extend_from_slice(key);
    let (min, max, sum) = (
        cents(a.amount_min),
        cents(a.amount_max),
        cents(a.amount_sum),
    );
    let rest = format!(
        " {} {} {sum} {min} {max} {} {} {}\n",
        a.count, a.id_sum, a.names_distinct, a.name_bytes, a.note_bytes
    );
    out.extend_from_slice(rest.as_bytes());
}

fn render(countries: &[[u8; 3]], groups: &[Agg]) -> Vec<u8> {
    let mut order: Vec<usize> = (0..countries.len()).collect();
    order.sort_unstable_by_key(|&g| countries[g]);
    let mut out = Vec::new();
    for g in order {
        if groups[g].count > 0 {
            write_line(&mut out, &countries[g], &groups[g]);
        }
    }
    // `_` (0x5F) sorts after every `[A-Z]{3}` code, so it is always last.
    let unmatched = &groups[countries.len()];
    if unmatched.count > 0 {
        write_line(&mut out, b"_UNMATCHED", unmatched);
    }
    out
}

fn main() {
    extern "C" { fn prctl(option: i32, arg2: u64, arg3: u64, arg4: u64, arg5: u64) -> i32; }
    // Keep sparsely touched tables from being charged as whole 2 MB pages.
    unsafe { prctl(41, 1, 0, 0, 0) }; // PR_SET_THP_DISABLE
    let (records, regions, output) = ("/data/records.csv", "/data/regions.csv", "/out/aggregates.txt");
    let threads = std::thread::available_parallelism().map_or(1, |n| n.get());
    let result = map_file(regions).and_then(|region_data| {
        let regions = Regions::new(region_data);
        let groups = aggregate(map_file(records)?, region_data, &regions, threads);
        std::fs::write(output, render(&regions.countries.into_inner().unwrap(), &groups))
    });
    if let Err(e) = result {
        eprintln!("track-c: failed: {e}");
        std::process::exit(1);
    }
    // Skip the destructors and the runtime's teardown.
    std::process::exit(0);
}
