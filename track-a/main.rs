// Speed Demon 2026, Track A (Ingest).
//
// One pool of threads parses and summarises; then, after a barrier and a short
// serial step, every thread formats.
//
// Parse. The log is memory-mapped and cut into line-aligned pieces that the
// threads claim in turn. The timestamp is always 27 bytes and the 72-hour span
// is fixed, so a line's hour index comes from its day and hour digits alone.
// One 64-byte scan after the timestamp finds the method's end, the endpoint's
// end and the line's end. All threads intern endpoints in one shared table
// and file a 4-byte record (latency, endpoint id, 5xx flag) under the hour.
//
// Hours. A thread takes the next hour as soon as it can be complete, and
// otherwise parses: the log is the shards' chunks in arrival order, so once the
// finished pieces from the front of the file reach a line of hour h + 2, the
// shards have shipped all of hour h. An hour's ~43k latencies are counting-sorted
// by endpoint id into per-thread scratch and three selections per group give
// the percentiles. Each group's numbers are kept along with the exact size the
// hour's text will take, since a number's width is known without formatting
// it. The hour's record chunks are then reused for later parsing. No bound on
// disorder is promised, so every piece checks on finishing that it holds no
// hour already taken; if one does, the program starts over without overlap.
//
// Text. Once every hour is taken, one thread ranks the names. Serial step: turn
// the sizes into offsets, size the output file and map it. Every thread then
// formats its hours straight into the mapped file, so no hour waits for another.
use std::arch::x86_64::*;
use std::cell::UnsafeCell;
use std::ffi::c_void;
use std::ptr::null_mut;
use std::slice::{from_raw_parts, from_raw_parts_mut};
use std::os::unix::process::CommandExt;
use std::sync::atomic::{AtomicBool, AtomicU16, AtomicU64, AtomicU8, AtomicUsize, Ordering::*};
use std::sync::{Barrier, Mutex};

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

const THREADS: usize = 8;
const PARTS: usize = THREADS * 8; // log pieces the threads claim in turn, as cores differ in speed
const HOURS: usize = 72; // 2026-10-30T18:00Z to 2026-11-02T17:59:59Z
// A record packs the latency above a 12-bit key: an 11-bit local endpoint id
// and the 5xx flag. Latencies of 2^20 us or more (about 1% of lines) do not
// fit and are kept on a side list with their key instead.
const KEY_BITS: u32 = 12;
const ERR: u32 = 1 << 11;
const ID_MASK: u32 = ERR - 1;
const PACKED_MAX: u32 = 1 << 20;

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

// Endpoint names to dense ids in first-seen order, shared by every thread.
// Open addressing on a CRC of the name's first and last eight bytes (masked
// when the name is shorter; the rest of the line keeps both loads in bounds).
// The first sight of a name copies it to a small shared arena, so later
// lookups do not touch old pieces of the log (which are already released). A
// free slot is claimed with BUSY, filled in, then published with its id + 1; a
// thread that finds a name only reads.
struct Interner {
    slot: Vec<AtomicU16>,
    name: Vec<AtomicU64>, // by id: the name's address with its length above bit 48
    arena: usize,         // 125 bytes at most per name, and 32 spare at the end
    used: AtomicUsize,
    count: AtomicUsize,
}

impl Interner {
    const SLOTS: usize = 1 << 13; // 8192 hash slots, independent of max 2048 ids
    const IDS: usize = 1 << 11; // compressed packed keys have 11-bit id
    const BUSY: u16 = u16::MAX;

    fn new() -> Self {
        let slot = (0..Self::SLOTS).map(|_| AtomicU16::new(0)).collect();
        let arena = unsafe { map(Self::IDS * 125 + 32, READ_WRITE, ANONYMOUS, -1) } as usize;
        Interner {
            slot,
            name: (0..Self::IDS).map(|_| AtomicU64::new(0)).collect(),
            arena,
            used: AtomicUsize::new(0),
            count: AtomicUsize::new(0),
        }
    }

    fn name(&self, id: usize) -> &'static [u8] {
        let x = self.name[id].load(Relaxed);
        unsafe { from_raw_parts((x & (1 << 48) - 1) as *const u8, (x >> 48) as usize) }
    }

    fn id(&self, s: &'static [u8]) -> u32 {
        let n = s.len();
        let word = |i: usize| unsafe { s.as_ptr().add(i).cast::<u64>().read_unaligned() };
        let a = word(0) & if n >= 8 { !0 } else { (1 << (8 * n)) - 1 };
        let b = if n >= 8 { word(n - 8) } else { 0 };
        let mut i = unsafe { _mm_crc32_u64(_mm_crc32_u64(n as u64, a), b) } as usize;
        loop {
            let at = &self.slot[i % Self::SLOTS];
            let mut id = at.load(Acquire);
            if id == 0 && at.compare_exchange(0, Self::BUSY, Acquire, Acquire).is_ok() {
                let new = self.count.fetch_add(1, Relaxed);
                assert!(new < Self::IDS, "too many endpoints"); // 11 bits
                let copy = self.arena + self.used.fetch_add(n, Relaxed);
                unsafe { (copy as *mut u8).copy_from_nonoverlapping(s.as_ptr(), n) };
                self.name[new].store(copy as u64 | (n as u64) << 48, Relaxed);
                at.store(new as u16 + 1, Release);
                id = new as u16 + 1;
            }
            while id == Self::BUSY || id == 0 {
                std::hint::spin_loop();
                id = at.load(Acquire);
            }
            if same(self.name(id as usize - 1), s) {
                return id as u32 - 1;
            }
            i += 1;
        }
    }
}
// 32 bytes can always be read from either name (the rest of the line and the
// spare page follow one, the arena the other), so a name of up to 32 bytes is
// compared in one step.
fn same(x: &[u8], s: &[u8]) -> bool {
    if x.len() != s.len() || s.len() > 32 {
        return x == s;
    }
    unsafe {
        let v = |p: &[u8]| _mm256_loadu_si256(p.as_ptr().cast());
        let eq = _mm256_movemask_epi8(_mm256_cmpeq_epi8(v(x), v(s))) as u32;
        !eq & (!0u32 >> (32 - s.len())) == 0
    }
}

// One hour's records from one thread, in a chain of 16 KB chunks. A chunk is
// reused from the free list, or else carved in order from a region the thread
// maps up front.
const CAP: usize = (16384 - 16) / 4;
#[repr(C)]
struct Chunk {
    prev: *mut Chunk,
    n: u32,
    rec: [u32; CAP],
}

static FREE: Mutex<Vec<usize>> = Mutex::new(Vec::new()); // chunks to reuse

struct Parser {
    last: [*mut Chunk; HOURS],
    wide: Vec<Vec<(u32, u32)>>, // (latency, key) of the long ones
    next: *mut Chunk,           // where the next chunk is carved
    base: *mut Chunk,           // where the carving began
}

impl Parser {
    // Room for `len` bytes of log: a line is at least 40 bytes, and each hour
    // may leave one chunk part full.
    fn new(len: usize) -> Self {
        let bytes = (len / 40 / CAP + 1 + HOURS) * size_of::<Chunk>();
        let next = unsafe { map(bytes, READ_WRITE, ANONYMOUS, -1).cast() };
        let wide = vec![vec![]; HOURS];
        Parser { last: [null_mut(); HOURS], wide, next, base: next }
    }

    // Parses the piece and returns the lowest and highest hour in it.
    unsafe fn parse(&mut self, ids: &Interner, mut p: *const u8, end: *const u8) -> (usize, usize) {
        let (mut lo, mut hi) = (HOURS, 0);
        // Exact date/hour fingerprint: YYYY-MM-DDTHH occupies bytes 5..12.
        // This is only a cache; changes always recompute the original hour.
        let mut prev_prefix = 0u64;
        let mut prev_hour = 0usize;
        while p < end {
            _mm_prefetch::<_MM_HINT_T0>(p.add(2048).cast()); // the log is read once, in order
            // YYYY-MM-DDTHH: month's last digit at 6, day at 8-9, hour at 11-12.
            let fingerprint = p.add(5).cast::<u64>().read_unaligned();
            let h = if fingerprint == prev_prefix {
                prev_hour
            } else {
                let d = |i: usize| (*p.add(i) - b'0') as usize;
                let day = d(8) * 10 + d(9) + if d(6) == 0 { 0 } else { 31 };
                let h = (day - 30) * 24 + d(11) * 10 + d(12) - 18;
                prev_prefix = fingerprint;
                prev_hour = h;
                h
            };
            (lo, hi) = (lo.min(h), hi.max(h));

            // Spaces and newlines in the 64 bytes after the timestamp. Only an
            // endpoint near its 125-byte maximum pushes past the window.
            let m = p.add(28);
            let sp = eq32(m, b' ') as u64 | (eq32(m.add(32), b' ') as u64) << 32;
            let nl = eq32(m, b'\n') as u64 | (eq32(m.add(32), b'\n') as u64) << 32;
            let at = |bits: u64| m.add(bits.trailing_zeros() as usize);
            let name = at(sp).add(1);
            let name_end = find(nl != 0, at(sp & (sp - 1)), name, b' ');
            let eol = find(nl != 0, at(nl), name_end.add(5), b'\n');
            let name_bytes = from_raw_parts(name, name_end.offset_from(name) as usize);
            let key = ids.id(name_bytes) | ERR * (*name_end.add(1) == b'5') as u32;
            let latency = decode(eol, eol.offset_from(name_end) as usize - 5); // after " SSS "
            p = eol.add(1);

            if latency >= PACKED_MAX {
                self.wide[h].push((latency, key));
                continue;
            }
            let c = &mut self.last[h];
            if c.is_null() || (**c).n as usize == CAP {
                let fresh = match FREE.lock().unwrap().pop() {
                    Some(a) => a as *mut Chunk,
                    None => {
                        self.next = self.next.add(1);
                        self.next.sub(1)
                    }
                };
                ((*fresh).prev, (*fresh).n) = (*c, 0);
                *c = fresh;
            }
            (**c).rec[(**c).n as usize] = latency << KEY_BITS | key;
            (**c).n += 1;
        }
        (lo, hi)
    }

    // Hands hour h's chunks to the free list, once they are read.
    unsafe fn recycle(&mut self, h: usize) {
        let mut c = std::mem::replace(&mut self.last[h], null_mut());
        let mut free = FREE.lock().unwrap();
        while !c.is_null() {
            free.push(c as usize);
            c = (*c).prev; // still ours: the lock is held
        }
        self.wide[h].clear(); // not freed: a thread filling the hour too late may still use it
    }

    // Calls f(id, 5xx, latency) for every record of hour h.
    unsafe fn each(&self, h: usize, mut f: impl FnMut(usize, bool, u32)) {
        let mut g = |latency: u32, key: u32| f((key & ID_MASK) as usize, key & ERR != 0, latency);
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

// Runs the program again, without overlap, in this process's place. One thread
// does it; any other waits to be replaced.
fn restart() -> ! {
    static ONCE: AtomicBool = AtomicBool::new(false);
    if !ONCE.swap(true, Relaxed) {
        let _ = std::process::Command::new("/proc/self/exe").arg("x").exec();
        std::process::exit(1);
    }
    loop {
        std::thread::park();
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
    // Where piece k starts: the first line start at or after its share of the bytes.
    let cut = |k: usize| {
        let mut at = size * k / PARTS;
        while at > 0 && at < size && log[at - 1] != b'\n' {
            at += 1;
        }
        at
    };

    let parsers: Vec<_> = (0..THREADS).map(|_| Shared::new(Parser::new(size))).collect();
    let claim_parse = AtomicUsize::new(0);
    let ids = Interner::new();
    let rank = Shared::new(Vec::<u32>::new()); // by id: the place of its name in byte order
    // Per piece: 0 until it is parsed, then its highest hour + 1.
    let top: Vec<AtomicU8> = (0..PARTS).map(|_| AtomicU8::new(0)).collect();
    let ranked = AtomicBool::new(false);
    let (claim, claim_text) = (AtomicUsize::new(0), AtomicUsize::new(0));
    // The restart keeps hours back until the whole log is parsed.
    // The restart keeps hours back until the whole log is parsed. Before it, an
    // hour taken too early can be read while a thread still fills it, which may
    // panic; that too only ends in the restart.
    let again = std::env::args_os().len() > 1;
    if !again {
        std::panic::set_hook(Box::new(|_| restart()));
    }
    let margin = if again { HOURS } else { 2 };
    // Per hour, in 12 pages of its own: its groups as (endpoint, count, p50, p95, p99, errors),
    // ended by a zero count. The pages go back to the kernel once the hour is written.
    const SLOT: usize = Interner::IDS * size_of::<[u32; 6]>();
    let lines = unsafe { map(HOURS * SLOT, READ_WRITE, ANONYMOUS, -1) } as usize;
    let at: Vec<AtomicU64> = (0..HOURS).map(|_| AtomicU64::new(0)).collect(); // size, then offset
    let text = Shared::new(null_mut::<u8>());
    let barrier = Barrier::new(THREADS);

    // Whether hour h is complete: the pieces finished from the front of the
    // file hold a line of hour h + margin, or are all of it.
    let ready = |h: usize| {
        let mut m = 0;
        for x in &top {
            match x.load(Acquire) {
                0 => return false,
                v => m = m.max(v as usize - 1),
            }
            if m >= h + margin {
                return true;
            }
        }
        true
    };

    let work = |t: usize| unsafe {
        const N: usize = Interner::IDS; // endpoint ids
        // Reuse a worker-local cursor across all hours: no per-hour allocations.
        let (mut start, mut errors, mut cursor, mut latency) =
            (vec![0u32; N + 1], vec![0u32; N], vec![0u32; N], vec![]);
        loop {
            let h = claim.load(Relaxed);
            if h < HOURS && ready(h) {
                if claim.compare_exchange(h, h + 1, AcqRel, Relaxed).is_err() {
                    continue;
                }
                // Counting sort of the hour's latencies by endpoint id: count into
                // start[e + 1], prefix, then place each latency at its group's cursor.
                start.fill(0);
                errors.fill(0);
                for ps in &parsers {
                    ps.get().each(h, |e, err, _| {
                        (start[e + 1], errors[e]) = (start[e + 1] + 1, errors[e] + err as u32)
                    });
                }
                for e in 0..N {
                    start[e + 1] += start[e];
                }
                latency.resize(start[N] as usize, 0);
                cursor.copy_from_slice(&start[..N]);
                for ps in &parsers {
                    ps.get().each(h, |e, _, l| (latency[cursor[e] as usize], cursor[e]) = (l, cursor[e] + 1));
                }
                parsers.iter().for_each(|ps| ps.get().recycle(h));

                let mut size = 0;
                let (slot, mut groups) = ((lines + h * SLOT) as *mut [u32; 6], 0);
                for e in 0..N {
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
                    let line = [e as u32, n as u32, p[0], p[1], p[2], err];
                    // Label, name, six numbers each after a space, newline.
                    let digits: usize = line[1..].iter().chain(&[ppm]).map(|&x| width(x)).sum();
                    size += 21 + ids.name(e).len() + 7 + digits;
                    slot.add(groups).write(line);
                    groups += 1;
                }
                at[h].store(size as u64, Relaxed);
            } else if let k @ 0..PARTS = claim_parse.fetch_add(1, Relaxed) {
                let (from, to) = (cut(k), cut(k + 1));
                let (lo, hi) = parsers[t].get().parse(&ids, log.as_ptr().add(from), log.as_ptr().add(to));
                // A line of an hour already taken: the arrival order failed, so
                // start over. Reading the counter with an exchange also makes
                // this piece's records visible to whoever takes an hour later.
                if lo < claim.fetch_add(0, AcqRel) {
                    restart();
                }
                top[k].store(hi as u8 + 1, Release);
                // Unmap the piece's whole pages now, in parallel, instead of all at exit.
                let (from, to) = ((from + 4095) & !4095, to & !4095);
                if to > from {
                    madvise(log.as_ptr().add(from) as *mut c_void, to - from, 4); // MADV_DONTNEED
                }
            } else if h >= HOURS {
                // Every piece is parsed, so the names are complete. Lines are printed
                // in unsigned byte order of the name within an hour (a proper prefix
                // first); one thread puts the ids in that order while the rest finish.
                if !ranked.swap(true, Relaxed) {
                    let all: Vec<_> = (0..ids.count.load(Relaxed)).map(|e| ids.name(e)).collect();
                    let mut order: Vec<u32> = (0..all.len() as u32).collect();
                    order.sort_unstable_by_key(|&e| all[e as usize]);
                    *rank.get() = vec![0; all.len()];
                    for (r, &e) in order.iter().enumerate() {
                        rank.get()[e as usize] = r as u32;
                    }
                }
                break;
            } else {
                std::hint::spin_loop();
            }
        }
        drop((start, errors, cursor, latency));

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
        // The output's pages are about to fill; give the chunks back first.
        let ps = parsers[t].get();
        madvise(ps.base.cast(), ps.next.byte_offset_from(ps.base) as usize, 4); // MADV_DONTNEED
        barrier.wait();

        while let h @ 0..HOURS = claim_text.fetch_add(1, Relaxed) {
            let date = ["10-30", "10-31", "11-01", "11-02"][(h + 18) / 24];
            let label = format!("2026-{date}T{:02}:00:00Z ", (h + 18) % 24);
            let mut o = (*text.get()).add(at[h].load(Relaxed) as usize);
            let base = (lines + h * SLOT) as *mut [u32; 6];
            let groups = (0..SLOT / size_of::<[u32; 6]>()).take_while(|&k| (*base.add(k))[1] != 0).count();
            let hour = from_raw_parts_mut(base, groups);
            hour.sort_unstable_by_key(|l| rank.get()[l[0] as usize]);
            for &[e, n, p50, p95, p99, err] in &*hour {
                for part in [label.as_bytes(), ids.name(e as usize)] {
                    o.copy_from_nonoverlapping(part.as_ptr(), part.len());
                    o = o.add(part.len());
                }
                for x in [n, p50, p95, p99, err, (err as u64 * 1_000_000 / n as u64) as u32] {
                    *o = b' ';
                    o = put(o.add(1), x);
                }
                *o = b'\n';
                o = o.add(1);
            }
            madvise(base.cast(), SLOT, 4); // MADV_DONTNEED
        }
    };
    // A failed spawn is ignored: it can only be the restart replacing the process.
    std::thread::scope(|s| {
        (0..THREADS).for_each(|t| _ = std::thread::Builder::new().spawn_scoped(s, move || work(t)))
    });
    std::process::exit(0); // skip the destructors
}
