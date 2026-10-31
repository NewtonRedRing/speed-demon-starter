// Speed Demon 2026, Track A (Ingest).
//
// One pool of threads does everything in three phases, with a barrier between
// them; at each barrier one thread does a short serial step.
//
// Parse. The log is memory-mapped and cut into line-aligned ranges, one per
// thread. The timestamp is always 27 bytes and the 72-hour span is fixed, so a
// line's hour index comes from its day and hour digits alone. One 64-byte scan
// after the timestamp finds the method's end, the endpoint's end and the
// line's end. Each thread interns endpoints in its own small table and files a
// 4-byte record (latency, local endpoint id, 5xx flag) under the hour.
// Serial step: merge the endpoint tables and sort the names, so an endpoint's
// global id is its rank in output order.
//
// Hours. Threads claim hours in order. An hour's ~43k latencies are
// counting-sorted by endpoint rank into per-thread scratch and three
// selections per group give the percentiles. Each group's numbers are kept
// along with the exact size the hour's text will take, since a number's width
// is known without formatting it. Serial step: turn the sizes into offsets,
// size the output file and map it.
//
// Text. Every thread formats its hours straight into the mapped file, so no
// hour waits for another and the output's pages fill only after the records
// have been released.
use std::arch::x86_64::*;
use std::cell::UnsafeCell;
use std::ffi::c_void;
use std::ptr::null_mut;
use std::slice::from_raw_parts;
use std::sync::atomic::{AtomicBool, AtomicU64, AtomicUsize, Ordering::*};
use std::sync::Barrier;

extern "C" {
    fn open(path: *const i8, flags: i32, ...) -> i32;
    fn mmap(addr: *mut c_void, len: usize, prot: i32, flags: i32, fd: i32, off: i64) -> *mut u8;
    fn madvise(addr: *mut c_void, len: usize, advice: i32) -> i32;
    fn ftruncate(fd: i32, len: i64) -> i32;
    fn prctl(option: i32, arg2: u64, arg3: u64, arg4: u64, arg5: u64) -> i32;
}
const READ_WRITE: i32 = 3; // PROT_READ | PROT_WRITE
const ANONYMOUS: i32 = 0x22; // MAP_PRIVATE | MAP_ANONYMOUS

// Zero-filled memory straight from the kernel; pages count only once written.
unsafe fn map(len: usize, prot: i32, flags: i32, fd: i32) -> *mut u8 {
    let p = mmap(null_mut(), len, prot, flags, fd, 0);
    assert!(p as isize != -1, "mmap failed");
    p
}

const THREADS: usize = 12;
const HOURS: usize = 72; // 2026-10-30T18:00Z to 2026-11-02T17:59:59Z
// A record packs the latency above a 13-bit key: a 12-bit shared endpoint
// slot and the 5xx flag. The shared slot removes the per-thread id merge.
const KEY_BITS: u32 = 13;
const ERR: u32 = 1 << 12;
const ID_MASK: u32 = ERR - 1;
const PACKED_MAX: u32 = 1 << 19;

// State the threads share, each touching its own part or waiting at a barrier
// while one thread writes.
struct Shared<T>(UnsafeCell<T>);
unsafe impl<T> Sync for Shared<T> {}
impl<T> Shared<T> {
    fn new(v: T) -> Self {
        Shared(UnsafeCell::new(v))
    }
    #[allow(clippy::mut_from_ref)]
    unsafe fn get(&self) -> &mut T {
        &mut *self.0.get()
    }
}

// Bit i is set when byte i of the 32 at p equals c.
#[inline(always)]
unsafe fn eq32(p: *const u8, c: u8) -> u32 {
    let v = _mm256_loadu_si256(p.cast());
    _mm256_movemask_epi8(_mm256_cmpeq_epi8(v, _mm256_set1_epi8(c as i8))) as u32
}

#[inline(always)]
unsafe fn markers64(p: *const u8) -> (u64, u64) {
    let half = |q: *const u8| {
        let v = _mm256_loadu_si256(q.cast());
        let sp = _mm256_movemask_epi8(_mm256_cmpeq_epi8(v, _mm256_set1_epi8(b' ' as i8))) as u32 as u64;
        let nl = _mm256_movemask_epi8(_mm256_cmpeq_epi8(v, _mm256_set1_epi8(b'\n' as i8))) as u32 as u64;
        (sp, nl)
    };
    let ((s0, n0), (s1, n1)) = (half(p), half(p.add(32)));
    (s0 | s1 << 32, n0 | n1 << 32)
}

// The byte the window found, or the first c from s on when the line runs past
// the window (one is known to follow).
unsafe fn find(in_window: bool, at: *const u8, mut s: *const u8, c: u8) -> *const u8 {
    if in_window {
        return at;
    }
    while eq32(s, c) == 0 {
        s = s.add(32);
    }
    s.add(eq32(s, c).trailing_zeros() as usize)
}

fn width(x: u32) -> usize {
    x.checked_ilog10().map_or(1, |d| d as usize + 1)
}

// The value of the len (1 to 10) digits ending just before end, with no loop:
// the 16 bytes before end, minus '0' and with the bytes ahead of the number
// zeroed, are combined 2, 4 and then 8 digits at a time.
#[inline(always)]
unsafe fn decode(end: *const u8, len: usize) -> u32 {
    let raw = _mm_loadu_si128(end.sub(16).cast());
    let v = _mm_subs_epu8(raw, _mm_set1_epi8(b'0' as i8));
    let lane = _mm_setr_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    let v = _mm_and_si128(v, _mm_cmpgt_epi8(lane, _mm_set1_epi8(15 - len as i8)));
    let v = _mm_maddubs_epi16(v, _mm_set1_epi16(0x010A)); // pairs (10, 1)
    let v = _mm_madd_epi16(v, _mm_set1_epi32(0x00010064)); // pairs (100, 1)
    let v = _mm_madd_epi16(_mm_packus_epi32(v, v), _mm_set1_epi32(0x00012710));
    let high = _mm_cvtsi128_si32(v) as u64;
    (high * 100_000_000 + _mm_extract_epi32::<1>(v) as u64) as u32
}

// One exact lock-free endpoint table shared by all workers. A slot is the
// endpoint id. New names are copied into one compact arena and published only
// after the bytes are ready; hashes select candidates but full bytes decide
// equality.
struct NameArena {
    bytes: Vec<u8>,
    ptr: *mut u8,
}
unsafe impl Send for NameArena {}
unsafe impl Sync for NameArena {}
impl NameArena {
    fn new(n: usize) -> Self {
        let mut bytes = vec![0u8; n];
        let ptr = bytes.as_mut_ptr();
        Self { bytes, ptr }
    }
    #[inline(always)]
    unsafe fn write(&self, off: usize, src: *const u8, n: usize) {
        std::ptr::copy_nonoverlapping(src, self.ptr.add(off), n);
    }
    #[inline(always)]
    unsafe fn slice(&self, off: usize, n: usize) -> &[u8] {
        std::slice::from_raw_parts(self.ptr.add(off), n)
    }
}

struct Names {
    slot: Vec<AtomicU64>,
    arena: NameArena,
}

impl Names {
    const SLOTS: usize = 1 << 12;
    const CELL: usize = 128;
    const BUSY: u64 = u64::MAX;

    fn new() -> Self {
        Self {
            slot: (0..Self::SLOTS).map(|_| AtomicU64::new(0)).collect(),
            arena: NameArena::new(Self::SLOTS * Self::CELL + 32),
        }
    }

    #[inline(always)]
    fn name(&self, v: u64) -> &[u8] {
        let off = (v as u32).wrapping_sub(1) as usize;
        let len = (v >> 32) as usize;
        unsafe { self.arena.slice(off, len) }
    }

    #[inline(always)]
    fn same(a: &[u8], b: &[u8]) -> bool {
        if a.len() != b.len() { return false; }
        if a.len() <= 32 {
            unsafe {
                let va = _mm256_loadu_si256(a.as_ptr().cast());
                let vb = _mm256_loadu_si256(b.as_ptr().cast());
                let eq = _mm256_movemask_epi8(_mm256_cmpeq_epi8(va, vb)) as u32;
                return (!eq & (!0u32 >> (32 - a.len()))) == 0;
            }
        }
        a == b
    }

    #[inline(always)]
    fn id(&self, s: &[u8]) -> u32 {
        let n = s.len();
        let a = if n >= 8 {
            unsafe { s.as_ptr().cast::<u64>().read_unaligned() }
        } else {
            let mut x = 0u64;
            unsafe { std::ptr::copy_nonoverlapping(s.as_ptr(), (&mut x as *mut u64).cast(), n) };
            x
        };
        let b = if n >= 8 {
            unsafe { s.as_ptr().add(n - 8).cast::<u64>().read_unaligned() }
        } else { 0 };
        let mut i = unsafe { _mm_crc32_u64(_mm_crc32_u64(n as u64, a), b) } as usize;
        for _ in 0..Self::SLOTS {
            let slot = i & (Self::SLOTS - 1);
            let mut v = self.slot[slot].load(Acquire);
            while v == Self::BUSY {
                std::hint::spin_loop();
                v = self.slot[slot].load(Acquire);
            }
            if v == 0 {
                if self.slot[slot].compare_exchange(0, Self::BUSY, Acquire, Acquire).is_ok() {
                    let off = slot * Self::CELL;
                    unsafe { self.arena.write(off, s.as_ptr(), n); }
                    let new = (n as u64) << 32 | (off as u64 + 1);
                    self.slot[slot].store(new, Release);
                    return slot as u32;
                }
                continue;
            }
            if Self::same(self.name(v), s) { return slot as u32; }
            i += 1;
        }
        panic!("endpoint table full");
    }
}

// One hour's records from one thread, in a chain of 16 KB chunks carved in
// order from a region the thread maps up front. The output file's pages count
// toward peak memory as it is written, so chunks are handed back while it
// grows: see Parser::release.
const CAP: usize = (16384 - 16) / 4;
#[repr(C)]
struct Chunk {
    prev: *mut Chunk,
    n: u32,
    hour: u32,
    rec: [u32; CAP],
}

struct Parser {
    last: [*mut Chunk; HOURS],
    wide: Vec<Vec<(u32, u32)>>,
    next: *mut Chunk,
    kept: *mut Chunk,
}

impl Parser {
    // Room for `len` bytes of log: a line is at least 40 bytes, and each hour
    // may leave one chunk part full.
    fn new(len: usize) -> Self {
        let bytes = (len / 40 / CAP + 1 + HOURS) * size_of::<Chunk>();
        let next = unsafe { map(bytes, READ_WRITE, ANONYMOUS, -1).cast() };
        let wide = vec![vec![]; HOURS];
        Parser { last: [null_mut(); HOURS], wide, next, kept: next }
    }

    unsafe fn parse(&mut self, endpoints: &Names, mut p: *const u8, end: *const u8) {
        while p < end {
            // YYYY-MM-DDTHH: month's last digit at 6, day at 8-9, hour at 11-12.
            let d = |i: usize| (*p.add(i) - b'0') as usize;
            let day = d(8) * 10 + d(9) + if d(6) == 0 { 0 } else { 31 };
            let h = (day - 30) * 24 + d(11) * 10 + d(12) - 18;

            // Spaces and newlines in the 64 bytes after the timestamp. Only an
            // endpoint near its 125-byte maximum pushes past the window.
            let m = p.add(28);
            let (sp, nl) = markers64(m);
            let at = |bits: u64| m.add(bits.trailing_zeros() as usize);
            let name = at(sp).add(1);
            let name_end = find(nl != 0, at(sp & (sp - 1)), name, b' ');
            let eol = find(nl != 0, at(nl), name_end.add(5), b'\n');
            let name_bytes = from_raw_parts(name, name_end.offset_from(name) as usize);
            let key = endpoints.id(name_bytes) | ERR * (*name_end.add(1) == b'5') as u32;
            let latency = decode(eol, eol.offset_from(name_end) as usize - 5); // after " SSS "
            p = eol.add(1);

            if latency >= PACKED_MAX {
                self.wide[h].push((latency, key));
                continue;
            }
            let c = &mut self.last[h];
            if c.is_null() || (**c).n as usize == CAP {
                let fresh = self.next;
                ((*fresh).prev, (*fresh).n, (*fresh).hour) = (*c, 0, h as u32);
                *c = fresh;
                self.next = fresh.add(1);
            }
            (**c).rec[(**c).n as usize] = latency << KEY_BITS | key;
            (**c).n += 1;
        }
    }

    // The log is in arrival order, so chunks are carved roughly hour by hour
    // and hours finish in the same order: the run of chunks at the front whose
    // hours are all finished grows steadily. Hand it back once it reaches 256
    // KB, in one madvise, instead of one call (and TLB shootdown) per chunk.
    unsafe fn release(&mut self, finished: &[AtomicBool]) {
        let mut c = self.kept;
        while c < self.next && finished[(*c).hour as usize].load(Acquire) {
            c = c.add(1);
        }
        if c.offset_from(self.kept) >= 16 || (c == self.next && c > self.kept) {
            let len = c.offset_from(self.kept) as usize * size_of::<Chunk>();
            madvise(self.kept.cast(), len, 4); // MADV_DONTNEED
            self.kept = c;
        }
    }

    // Calls f(global rank, 5xx, latency) for every record of hour h.
    unsafe fn each(&self, h: usize, ranks: &[u32], mut f: impl FnMut(usize, bool, u32)) {
        let rank = |key: u32| ranks[(key & ID_MASK) as usize] as usize;
        let mut g = |latency: u32, key: u32| f(rank(key), key & ERR != 0, latency);
        let mut c = self.last[h];
        while let Some(chunk) = c.as_ref() {
            chunk.rec[..chunk.n as usize].iter().for_each(|&r| g(r >> KEY_BITS, r));
            c = chunk.prev;
        }
        for &(latency, key) in &self.wide[h] {
            g(latency, key);
        }
    }
}

// Writes x in decimal at o, returns the end.
unsafe fn put(o: *mut u8, mut x: u32) -> *mut u8 {
    let end = o.add(width(x));
    let mut q = end;
    loop {
        q = q.sub(1);
        *q = b'0' + (x % 10) as u8;
        x /= 10;
        if x == 0 {
            return end;
        }
    }
}

fn main() {
    // With transparent huge pages, a chunk region written only in part could
    // be backed by whole 2 MB pages and count against peak memory.
    unsafe { prctl(41, 1, 0, 0, 0) }; // PR_SET_THP_DISABLE
    let input = unsafe { open(c"/data/requests.log".as_ptr(), 0) };
    let out = unsafe { open(c"/out/summary.txt".as_ptr(), 0o1102, 0o644) };
    assert!(input >= 0 && out >= 0, "open failed"); // O_RDWR | O_CREAT | O_TRUNC

    // Map the log over one spare zero page, so the 64-byte scans stay in bounds.
    let size = std::fs::metadata("/data/requests.log").expect("stat").len() as usize;
    let log: &'static [u8] = unsafe {
        let p = map(size + 4096, 1, ANONYMOUS, -1);
        if size > 0 {
            mmap(p.cast(), size, 1, 0x12, input, 0); // PROT_READ, MAP_PRIVATE | MAP_FIXED
        }
        from_raw_parts(p, size)
    };
    let mut cut = vec![size; THREADS + 1];
    cut[0] = 0;
    for t in 1..THREADS {
        cut[t] = cut[t - 1].max(size * t / THREADS);
        while cut[t] > 0 && cut[t] < size && log[cut[t] - 1] != b'\n' {
            cut[t] += 1;
        }
    }

    let parser = |t: usize| Shared::new(Parser::new(cut[t + 1] - cut[t]));
    let parsers: Vec<_> = (0..THREADS).map(parser).collect();
    let endpoints = Names::new();
    let names = Shared::new(Vec::<Vec<u8>>::new());
    let ranks = Shared::new(vec![0u32; Names::SLOTS]);
    let finished: Vec<AtomicBool> = (0..HOURS).map(|_| AtomicBool::new(false)).collect();
    let releasing = AtomicBool::new(false);
    let (claim, claim_text) = (AtomicUsize::new(0), AtomicUsize::new(0));
    // Per hour: its groups as (endpoint, count, p50, p95, p99, errors, error ppm).
    let lines: Vec<Shared<Vec<[u32; 7]>>> = (0..HOURS).map(|_| Shared::new(vec![])).collect();
    let at: Vec<AtomicU64> = (0..HOURS).map(|_| AtomicU64::new(0)).collect(); // size, then offset
    let text = Shared::new(null_mut::<u8>());
    let barrier = Barrier::new(THREADS);

    let work = |t: usize| unsafe {
        let (from, to) = (log.as_ptr().add(cut[t]), log.as_ptr().add(cut[t + 1]));
        parsers[t].get().parse(&endpoints, from, to);

        // Slot ids are already global. Only rank the ~1,842 occupied slots.
        if barrier.wait().is_leader() {
            let mut by_name: Vec<(Vec<u8>, u32)> = endpoints.slot.iter().enumerate()
                .filter_map(|(id, s)| {
                    let v = s.load(Acquire);
                    (v != 0).then(|| (endpoints.name(v).to_vec(), id as u32))
                }).collect();
            by_name.sort_unstable_by(|a, b| a.0.cmp(&b.0));
            let rank = ranks.get();
            let out = names.get();
            out.clear();
            for (r, (name, id)) in by_name.into_iter().enumerate() {
                rank[id as usize] = r as u32;
                out.push(name);
            }
        }
        barrier.wait();

        let names = &*names.get();
        let rank = &*ranks.get();
        let ne = names.len();
        let (mut start, mut errors, mut latency) = (vec![0u32; ne + 1], vec![0u32; ne], vec![]);
        while let h @ 0..HOURS = claim.fetch_add(1, Relaxed) {
            // Counting sort of the hour's latencies by endpoint rank: count into
            // start[e + 1], prefix, then place each latency at its group's cursor.
            start.fill(0);
            errors.fill(0);
            for ps in &parsers {
                ps.get().each(h, rank, |e, err, _| (start[e + 1], errors[e]) = (start[e + 1] + 1, errors[e] + err as u32));
            }
            for e in 0..ne {
                start[e + 1] += start[e];
            }
            latency.resize(start[ne] as usize, 0);
            let mut cursor = start[..ne].to_vec();
            for ps in &parsers {
                ps.get().each(h, rank, |e, _, l| (latency[cursor[e] as usize], cursor[e]) = (l, cursor[e] + 1));
            }
            // An hour is finished once its records are copied out; one thread
            // at a time then releases whatever chunks that frees.
            finished[h].store(true, Release);
            if !releasing.swap(true, Acquire) {
                parsers.iter().for_each(|ps| ps.get().release(&finished));
                releasing.store(false, Release);
            }

            let mut size = 0;
            for e in 0..ne {
                let a = &mut latency[start[e] as usize..start[e + 1] as usize];
                let n = a.len();
                if n == 0 {
                    continue;
                }
                // Nearest rank p is index ceil(p * n / 100) - 1. Selecting the
                // highest rank first leaves everything below it in front, so
                // each later selection only needs the front part.
                let r = [50, 95, 99].map(|p| (p * n + 99) / 100 - 1);
                if n <= 64 {
                    a.sort_unstable(); // most groups; cheaper than three selections
                } else {
                    a.select_nth_unstable(r[2]);
                    a[..r[2]].select_nth_unstable(r[1]);
                    a[..r[1]].select_nth_unstable(r[0]);
                }
                let (p, err) = (r.map(|i| a[i]), errors[e]);
                let ppm = (err as u64 * 1_000_000 / n as u64) as u32;
                let line = [e as u32, n as u32, p[0], p[1], p[2], err, ppm];
                // Label, name, six numbers each after a space, newline.
                let digits: usize = line[1..].iter().map(|&x| width(x)).sum();
                size += 21 + names[e].len() + 7 + digits;
                lines[h].get().push(line);
            }
            at[h].store(size as u64, Relaxed);
        }

        if barrier.wait().is_leader() {
            let mut total = 0;
            for size in &at {
                total += size.swap(total, Relaxed);
            }
            if total > 0 {
                assert!(ftruncate(out, total as i64) == 0, "ftruncate failed");
                *text.get() = map(total as usize, READ_WRITE, 1, out); // MAP_SHARED
            }
        }
        barrier.wait();

        while let h @ 0..HOURS = claim_text.fetch_add(1, Relaxed) {
            let date = ["10-30", "10-31", "11-01", "11-02"][(h + 18) / 24];
            let label = format!("2026-{date}T{:02}:00:00Z ", (h + 18) % 24);
            let mut o = (*text.get()).add(at[h].load(Relaxed) as usize);
            for line in std::mem::take(lines[h].get()) {
                let name = names[line[0] as usize].as_slice();
                for part in [label.as_bytes(), name] {
                    o.copy_from_nonoverlapping(part.as_ptr(), part.len());
                    o = o.add(part.len());
                }
                for &x in &line[1..] {
                    *o = b' ';
                    o = put(o.add(1), x);
                }
                *o = b'\n';
                o = o.add(1);
            }
        }
    };
    std::thread::scope(|s| (0..THREADS).for_each(|t| _ = s.spawn(move || work(t))));
}
