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
//!   its raw bytes; escaping is one to one, so equal names have equal raw
//!   bytes) and one bit per (country, name number). A country's distinct
//!   count is the popcount of its row. Lookups are batched and prefetched
//!   so their cache misses overlap.
//! - The file is parsed in parallel, in many more chunks than threads that
//!   workers claim from a counter (cores differ in speed). A chunk cut is
//!   the first LF that is followed by a digit or a BOM, which spec 2.2
//!   makes a record boundary: names are letters and notes lowercase words.
//! - The last few KB are copied behind zero padding so the SIMD loads never
//!   run past the mapping.

use std::arch::x86_64::{
    __m128i, __m256i, _mm_clmulepi64_si128, _mm_crc32_u64, _mm_cvtsi128_si64,
    _mm_cvtsi64_si128, _mm_set1_epi8, _mm256_cmpeq_epi8, _mm256_loadu_si256,
    _mm256_movemask_epi8, _mm256_set1_epi8,
};
use std::cell::UnsafeCell;
use std::sync::atomic::{AtomicU32, AtomicU64, AtomicUsize, Ordering::{Acquire, Relaxed, Release}};

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
/// fits one byte with no overflow scheme needed.
struct Regions {
    countries: Vec<[u8; 3]>,
    group_of: Vec<u8>,
}

fn parse_regions(data: &[u8]) -> Regions {
    // Spec 2.3: a code is three bytes `A`-`Z`, so five bits each identify it.
    let mut index = vec![0u16; 1 << 15];
    let mut countries = Vec::new();
    // Ids ascend, so the last line has the largest; size the table once.
    let last = data.rsplit(|&b| b == b'\n').nth(1).unwrap_or(&[]);
    let max_id = last
        .iter()
        .take_while(|d| d.is_ascii_digit())
        .fold(0usize, |a, &d| a * 10 + (d - b'0') as usize);
    let mut group_of = vec![0u8; max_id + 1];
    let mut at = data
        .iter()
        .position(|&b| b == b'\n')
        .map_or(data.len(), |p| p + 1);
    while at < data.len() {
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
        let slot = &mut index[code.iter().fold(0usize, |a, &c| a << 5 | (c & 31) as usize)];
        if *slot == 0 {
            countries.push([code[0], code[1], code[2]]);
            *slot = countries.len() as u16;
        }
        if id >= group_of.len() {
            group_of.resize(id + 1, 0);
        }
        group_of[id] = *slot as u8;
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
    Regions {
        countries,
        group_of,
    }
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
/// exactly in the slot. Longer names store an exact first eight bytes plus a
/// marked full-name hash as a candidate filter, then compare every remaining
/// byte from the side arena before equality is accepted.
const NAME_SLOTS: usize = 1 << 17;
const NAME_WORDS: usize = NAME_SLOTS / 64;
const NAME_ARENA: usize = 1 << 20;
const OCCUPIED: u64 = 1 << 63;
const BUSY: u64 = u64::MAX;
const LONG: u64 = 1 << 63;

#[repr(C, align(16))]
struct NameSlot {
    lo: AtomicU64,
    hi: UnsafeCell<u64>,
}
unsafe impl Sync for NameSlot {}

struct MetaCell(UnsafeCell<u64>);
unsafe impl Sync for MetaCell {}

struct ByteArena(UnsafeCell<Vec<u8>>);
unsafe impl Sync for ByteArena {}

struct Names {
    slots: Vec<NameSlot>,
    meta: Vec<MetaCell>, // long only: raw_len << 32 | tail_offset
    arena: ByteArena,
    used: AtomicU32,
    bits: Vec<AtomicU64>,
}

fn zeroed(n: usize) -> Vec<AtomicU64> {
    let mut v = std::mem::ManuallyDrop::new(vec![0u64; n]);
    unsafe { Vec::from_raw_parts(v.as_mut_ptr().cast(), v.len(), v.capacity()) }
}

#[inline(always)]
fn byte_mask(n: usize) -> u64 {
    if n >= 8 { !0 } else if n == 0 { 0 } else { (1u64 << (n * 8)) - 1 }
}

#[inline(always)]
unsafe fn name_words(p: usize, len: usize, h: u64) -> (u64, u64) {
    let q = p as *const u8;
    let lo = q.cast::<u64>().read_unaligned() & byte_mask(len);
    let hi = if len <= 8 {
        0
    } else if len <= 16 {
        q.add(8).cast::<u64>().read_unaligned() & byte_mask(len - 8)
    } else {
        (h & !LONG) | LONG
    };
    (lo, hi)
}

impl Names {
    fn new(groups: usize) -> Self {
        Self {
            slots: (0..NAME_SLOTS).map(|_| NameSlot {
                lo: AtomicU64::new(0),
                hi: UnsafeCell::new(0),
            }).collect(),
            meta: (0..NAME_SLOTS).map(|_| MetaCell(UnsafeCell::new(0))).collect(),
            arena: ByteArena(UnsafeCell::new(vec![0u8; NAME_ARENA])),
            used: AtomicU32::new(0),
            bits: zeroed(groups * NAME_WORDS),
        }
    }

    #[inline(always)]
    fn long_same(&self, slot: usize, p: usize, len: usize) -> bool {
        let m = unsafe { *self.meta[slot].0.get() };
        if (m >> 32) as usize != len { return false; }
        let off = m as u32 as usize;
        let tail = len - 16;
        let arena = unsafe { &*self.arena.0.get() };
        &arena[off..off + tail]
            == unsafe { std::slice::from_raw_parts((p as *const u8).add(16), tail) }
    }

    #[inline(always)]
    fn number(&self, h: u64, p: usize, len: usize) -> usize {
        let (raw_lo, key_hi) = unsafe { name_words(p, len, h) };
        let ready_lo = raw_lo | OCCUPIED;
        let mut i = h as usize & (NAME_SLOTS - 1);
        loop {
            let mut lo = self.slots[i].lo.load(Acquire);
            while lo == BUSY {
                std::hint::spin_loop();
                lo = self.slots[i].lo.load(Acquire);
            }
            if lo == 0 {
                if self.slots[i].lo.compare_exchange(0, BUSY, Acquire, Acquire).is_ok() {
                    if len > 16 {
                        let tail = len - 16;
                        let off = self.used.fetch_add(tail as u32, Relaxed) as usize;
                        assert!(off + tail <= NAME_ARENA, "name arena exhausted");
                        unsafe {
                            std::ptr::copy_nonoverlapping(
                                (p as *const u8).add(16),
                                (&mut *self.arena.0.get()).as_mut_ptr().add(off),
                                tail,
                            );
                            *self.meta[i].0.get() = (len as u64) << 32 | off as u64;
                            *self.slots[i].hi.get() = key_hi;
                        }
                    } else {
                        unsafe { *self.slots[i].hi.get() = key_hi; }
                    }
                    self.slots[i].lo.store(ready_lo, Release);
                    return i;
                }
                continue;
            }
            if (lo & !OCCUPIED) == raw_lo
                && unsafe { *self.slots[i].hi.get() } == key_hi
                && (len <= 16 || self.long_same(i, p, len))
            {
                return i;
            }
            i = (i + 1) & (NAME_SLOTS - 1);
        }
    }

    fn distinct(&self, group: usize) -> u64 {
        self.bits[group * NAME_WORDS..(group + 1) * NAME_WORDS]
            .iter().map(|w| w.load(Relaxed).count_ones() as u64).sum()
    }
}

const BATCH: usize = 16;

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
        prefetch_read(&self.names.slots[home] as *const NameSlot);
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
    let mut h = len as u64;
    let mut p = from;
    unsafe {
        while p + 8 <= to {
            h = _mm_crc32_u64(h, (data.as_ptr().add(p) as *const u64).read_unaligned());
            p += 8;
        }
        if p < to {
            let mut tail = 0u64;
            std::ptr::copy_nonoverlapping(data.as_ptr().add(p), (&mut tail as *mut u64).cast(), to - p);
            h = _mm_crc32_u64(h, tail);
        }
    }
    h.wrapping_mul(0x9E37_79B9_7F4A_7C15) ^ (h.rotate_left(29))
}

// --- Record parsing (spec section 2.1) ----------------------------------

/// Bit mask of bytes equal to c in data[at..at+32].
#[inline(always)]
fn match32(data: &[u8], at: usize, c: u8) -> u32 {
    unsafe {
        let v = _mm256_loadu_si256(data.as_ptr().add(at).cast::<__m256i>());
        _mm256_movemask_epi8(_mm256_cmpeq_epi8(v, _mm256_set1_epi8(c as i8))) as u32
    }
}

#[inline(always)]
fn match64(data: &[u8], at: usize, c: u8) -> u64 {
    match32(data, at, c) as u64 | ((match32(data, at + 32, c) as u64) << 32)
}

#[derive(Clone, Copy)]
struct Separator {
    at: usize,
    quotes: u64,
}

/// Streams commas/newlines that are outside quoted CSV fields. Quote parity
/// for all 64 bytes is computed in one carry-less multiply (prefix XOR), so
/// names and notes are not rescanned byte-by-byte just to locate separators.
struct Structure<'a> {
    data: &'a [u8],
    block: usize,
    bits: u64,
    carry: u64,
    q: u64,
    before: u64,
}

impl<'a> Structure<'a> {
    #[inline]
    fn new(data: &'a [u8], start: usize) -> Self {
        Self { data, block: start, bits: 0, carry: 0, q: 0, before: 0 }
    }

    #[inline(always)]
    fn next(&mut self) -> Separator {
        while self.bits == 0 {
            if self.block + 64 > self.data.len() {
                return Separator { at: self.data.len(), quotes: self.before + self.q.count_ones() as u64 };
            }
            let at = self.block;
            self.block += 64;
            self.before += self.q.count_ones() as u64;
            self.q = match64(self.data, at, b'"');
            let structural = match64(self.data, at, b',') | match64(self.data, at, b'\n');
            if self.q == 0 && self.carry == 0 {
                self.bits = structural;
            } else {
                let prefix = unsafe {
                    let x: __m128i = _mm_clmulepi64_si128::<0x00>(
                        _mm_cvtsi64_si128(self.q as i64),
                        _mm_set1_epi8(-1),
                    );
                    _mm_cvtsi128_si64(x) as u64
                } ^ self.carry;
                self.carry = ((prefix as i64) >> 63) as u64;
                self.bits = structural & !prefix;
            }
        }
        let i = self.bits.trailing_zeros() as usize;
        self.bits &= self.bits - 1;
        let before_bit = if i == 0 { 0 } else { (1u64 << i) - 1 };
        Separator {
            at: self.block - 64 + i,
            quotes: self.before + (self.q & before_bit).count_ones() as u64,
        }
    }
}

#[inline(always)]
fn decoded_text_len(raw_len: usize, quote_bytes: u64) -> usize {
    raw_len - if quote_bytes == 0 { 0 } else { quote_bytes as usize / 2 + 1 }
}

/// The run of ASCII digits at `data[at..]` as (value, length).
#[inline]
fn digits(data: &[u8], at: usize) -> (u64, usize) {
    let w = u64::from_le_bytes(data[at..at + 8].try_into().unwrap());
    let not_digit = (w & 0xF0F0_F0F0_F0F0_F0F0) ^ 0x3030_3030_3030_3030;
    let mut n = not_digit.trailing_zeros() as usize / 8;
    // Digits to the top bytes, so the first one lands in the highest decimal place.
    let w = (w << (64 - 8 * n.max(1)) & 0x0F0F_0F0F_0F0F_0F0F).wrapping_mul(2561) >> 8;
    let w = ((w & 0x00FF_00FF_00FF_00FF).wrapping_mul(6_553_601)) >> 16;
    let mut v = ((w & 0x0000_FFFF_0000_FFFF).wrapping_mul(42_949_672_960_001)) >> 32;
    while n >= 8 && data[at + n].is_ascii_digit() {
        v = v * 10 + (data[at + n] - b'0') as u64;
        n += 1;
    }
    (v, n)
}

/// Parses every record in `data[start..end]` (both record boundaries; the
/// 64 bytes after `end` must be readable) into `groups`.
#[derive(Clone, Copy, Default)]
struct Pending {
    id: u64,
    region: usize,
    cents: i64,
    name_bytes: u64,
    note_bytes: u64,
    hash: u64,
    ptr: usize,
    len: usize,
}

#[inline(always)]
fn retire_pending(
    p: Pending,
    regions: &Regions,
    unmatched: usize,
    sightings: &mut Sightings,
    groups: &mut [Agg],
) {
    let g = if p.region < regions.group_of.len() {
        let x = unsafe { *regions.group_of.get_unchecked(p.region) };
        if x != 0 { x as usize - 1 } else { unmatched }
    } else {
        unmatched
    };
    let agg = unsafe { groups.get_unchecked_mut(g) };
    agg.count += 1;
    agg.id_sum += p.id;
    agg.amount_sum += p.cents;
    agg.amount_min = agg.amount_min.min(p.cents);
    agg.amount_max = agg.amount_max.max(p.cents);
    agg.name_bytes += p.name_bytes;
    agg.note_bytes += p.note_bytes;
    sightings.push(g, p.hash, p.ptr, p.len);
}

/// Parses every record in data[start..end]. Region lookup and aggregation are
/// deliberately retired 16 records after parse so the random region-table
/// read overlaps with CSV scanning and name hashing.
fn parse_records(
    data: &[u8],
    start: usize,
    end: usize,
    regions: &Regions,
    sightings: &mut Sightings,
    groups: &mut [Agg],
) {
    let unmatched = regions.countries.len();
    let mut pending = [Pending::default(); BATCH];
    let mut pending_len = 0usize;
    let mut i = start;
    let mut sep = Structure::new(data, start);
    while i < end {
        prefetch_read(data.as_ptr().wrapping_add(i + 1536));
        i += 3 * (data[i] == 0xEF) as usize;

        let f1 = sep.next();
        let f2 = sep.next();
        let f3 = sep.next();
        let f4 = sep.next();
        let eol = sep.next();

        let (id, _) = digits(data, i);
        let (region, _) = digits(data, f3.at + 1);

        let mut a = f4.at + 1;
        let neg = data[a] == b'-';
        a += neg as usize;
        let (whole, n) = digits(data, a);
        let frac = a + n + 1;
        let cents = (whole * 100
            + (data[frac] - b'0') as u64 * 10
            + (data[frac + 1] - b'0') as u64) as i64;
        let cents = if neg { -cents } else { cents };

        let nq = f2.quotes - f1.quotes;
        let tq = f3.quotes - f2.quotes;
        let quoted = nq != 0;
        let ns = f1.at + 1 + quoted as usize;
        let ne = f2.at - quoted as usize;
        let region = region as usize;
        if region < regions.group_of.len() {
            prefetch_read(unsafe { regions.group_of.as_ptr().add(region) });
        }

        pending[pending_len] = Pending {
            id,
            region,
            cents,
            name_bytes: decoded_text_len(f2.at - (f1.at + 1), nq) as u64,
            note_bytes: decoded_text_len(f3.at - (f2.at + 1), tq) as u64,
            hash: hash_name(data, ns, ne),
            ptr: data.as_ptr().wrapping_add(ns) as usize,
            len: ne - ns,
        };
        pending_len += 1;
        if pending_len == BATCH {
            for &x in &pending {
                retire_pending(x, regions, unmatched, sightings, groups);
            }
            pending_len = 0;
        }
        i = eol.at + 1;
    }
    for &x in &pending[..pending_len] {
        retire_pending(x, regions, unmatched, sightings, groups);
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

/// Bytes at the end of the file that are copied behind padding and parsed
/// last, so no chunk's SIMD loads can run past the mapping.
const TAIL: usize = 8192;
const CHUNKS_PER_THREAD: usize = 32;

fn scan(data: &[u8], body: usize, regions: &Regions, threads: usize, names: &Names) -> Vec<Agg> {
    let n_groups = regions.countries.len() + 1;
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
    let next = AtomicUsize::new(0);
    let mut parts: Vec<Vec<Agg>> = std::thread::scope(|s| {
        let handles: Vec<_> = (0..threads)
            .map(|_| {
                s.spawn(|| {
                    let mut groups = vec![Agg::new(); n_groups];
                    let mut sightings = Sightings::new(names);
                    loop {
                        let k = next.fetch_add(1, Relaxed);
                        if k >= chunks {
                            sightings.finish();
                            return groups;
                        }
                        parse_records(
                            data,
                            bound(k),
                            bound(k + 1),
                            regions,
                            &mut sightings,
                            &mut groups,
                        );
                    }
                })
            })
            .collect();
        handles
            .into_iter()
            .map(|h| h.join().expect("parse worker panicked"))
            .collect()
    });

    let mut tail = data[tail_at..].to_vec();
    tail.resize(tail.len() + 64, 0);
    let mut groups = vec![Agg::new(); n_groups];
    let mut sightings = Sightings::new(names);
    parse_records(
        &tail,
        0,
        data.len() - tail_at,
        regions,
        &mut sightings,
        &mut groups,
    );
    sightings.finish();
    parts.push(groups);

    let mut out = vec![Agg::new(); n_groups];
    for part in &parts {
        for (dst, src) in out.iter_mut().zip(part) {
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

fn aggregate(data: &[u8], regions: &Regions, threads: usize) -> Vec<Agg> {
    let n_groups = regions.countries.len() + 1;
    let body = data.iter().position(|&b| b == b'\n').map_or(data.len(), |p| p + 1);
    if body >= data.len() { return vec![Agg::new(); n_groups]; }
    let names = Names::new(n_groups);
    let mut out = scan(data, body, regions, threads.max(1), &names);
    out.iter_mut().enumerate()
        .for_each(|(g, a)| a.names_distinct = names.distinct(g));
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

fn render(regions: &Regions, groups: &[Agg]) -> Vec<u8> {
    let mut order: Vec<usize> = (0..regions.countries.len()).collect();
    order.sort_unstable_by_key(|&g| regions.countries[g]);
    let mut out = Vec::new();
    for g in order {
        if groups[g].count > 0 {
            write_line(&mut out, &regions.countries[g], &groups[g]);
        }
    }
    // `_` (0x5F) sorts after every `[A-Z]{3}` code, so it is always last.
    let unmatched = &groups[regions.countries.len()];
    if unmatched.count > 0 {
        write_line(&mut out, b"_UNMATCHED", unmatched);
    }
    out
}

fn main() {
    let var =
        |name: &str, default: &str| std::env::var(name).unwrap_or_else(|_| default.to_string());
    let (records, regions, output) = (
        var("RECORDS_PATH", "/data/records.csv"),
        var("REGIONS_PATH", "/data/regions.csv"),
        var("OUTPUT_PATH", "/out/aggregates.txt"),
    );
    let threads = std::thread::available_parallelism().map_or(1, |n| n.get());
    let result = map_file(&regions).and_then(|regions_data| {
        let regions = parse_regions(regions_data);
        let groups = aggregate(map_file(&records)?, &regions, threads);
        std::fs::write(&output, render(&regions, &groups))
    });
    if let Err(e) = result {
        eprintln!("track-c: failed: {e}");
        std::process::exit(1);
    }
    // Skip the destructors and the runtime's teardown.
    std::process::exit(0);
}
