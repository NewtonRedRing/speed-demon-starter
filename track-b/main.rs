//! Speed Demon 2026, Track B (Index), in Rust with no dependencies.
//!
//! 1. The queries are read first, and only their terms are looked for.
//! 2. Each thread samples the start of its share of docs.txt, counting the
//!    documents that hold each term: a BOOL term in over 1/32 of them is kept
//!    as a bitmap, and each phrase's rarest term becomes its anchor.
//! 3. The full scan. Every phrase is checked wherever its anchor occurs, while
//!    the text is in cache. A BOOL term in a document sets its bitmap bit or
//!    files the pair under the term's bucket; the buckets are then scattered
//!    into one stretch of 16-bit doc offsets per term.
//! 4. BOOL queries run left to right on sorted lists, or on a bitmap once a
//!    bitmap term starts the query or is OR-ed in.
use std::arch::x86_64::*;
use std::borrow::Cow;
use std::fmt::Write;
use std::os::fd::AsRawFd;
use std::sync::atomic::{AtomicU16, AtomicU64, AtomicUsize, Ordering::Relaxed};
use std::sync::Mutex;

extern "C" {
    fn mmap(addr: *mut u8, len: usize, prot: i32, flags: i32, fd: i32, off: i64) -> *mut u8;
    fn mallopt(param: i32, value: i32) -> i32;
    fn prctl(option: i32, ...) -> i32;
    fn madvise(addr: *mut u8, len: usize, advice: i32) -> i32;
}

extern "C" {
    fn track_b_find_c(table: *const u8, mask: usize, key: *const u8, hash: usize, n: u32) -> u32;
}

const THREADS: usize = 12;
const NONE: u32 = u32::MAX;
const BUCKETS: usize = 32; // scan output streams per thread; term i goes to bucket i % BUCKETS
const GAP: u32 = 15; // a unit's 4-bit doc gap; GAP means the whole gap follows in the next unit
const CHUNK: usize = 1 << 16; // bytes of one scan output chunk
const DOC_BITS: u32 = 15; // a thread's share holds under 2^15 documents

/// The file, mapped over a spare zero page so loads of 32 bytes past any of
/// its bytes stay inside the mapping.
fn map(path: &str) -> &'static [u8] {
    let file = std::fs::File::open(path).expect(path);
    let len = file.metadata().unwrap().len() as usize;
    unsafe {
        let p = mmap(std::ptr::null_mut(), len + 4096, 1, 0x22, -1, 0); // PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS
        let ok = len == 0 || mmap(p, len, 1, 0x12, file.as_raw_fd(), 0) == p; // MAP_PRIVATE | MAP_FIXED
        assert!(p as isize != -1 && ok, "mmap {path}");
        std::slice::from_raw_parts(p, len)
    }
}

/// Runs f(t) on every thread and collects what each returns.
fn on_threads<R: Send>(f: impl Fn(usize) -> R + Sync) -> Vec<R> {
    let f = &f;
    std::thread::scope(|s| {
        let hs: Vec<_> = (0..THREADS).map(|t| s.spawn(move || f(t))).collect();
        hs.into_iter().map(|h| h.join().unwrap()).collect()
    })
}

/// The decimal number at text[i..].
fn number(text: &[u8], i: usize) -> u32 {
    let digits = text[i..].iter().take_while(|b| b.is_ascii_digit());
    digits.fold(0, |v, &b| v * 10 + (b - b'0') as u32)
}

/// Token bytes: [A-Za-z0-9].
static WORD: [bool; 256] = {
    let mut t = [false; 256];
    let mut c = 0;
    while c < 256 {
        (t[c], c) = ((c as u8).is_ascii_alphanumeric(), c + 1);
    }
    t
};

/// The key of the token of len (at most 32) bytes at p: its bytes lowercased
/// (OR-ing 0x20 lowercases letters and keeps digits) and zero padded.
/// The KEEP mask is loaded at 32 - len, so its first len bytes are 0xFF.
unsafe fn key(p: *const u8, len: usize) -> __m256i {
    static KEEP: [u8; 64] = {
        let (mut k, mut i) = ([0; 64], 0);
        while i < 32 {
            (k[i], i) = (0xFF, i + 1);
        }
        k
    };
    let v = _mm256_or_si256(_mm256_loadu_si256(p.cast()), _mm256_set1_epi8(0x20));
    _mm256_and_si256(v, _mm256_loadu_si256(KEEP.as_ptr().add(32 - len).cast()))
}

/// The hash of a key of at most 12 bytes.
fn hash(k: __m128i) -> usize {
    let w: [u64; 2] = unsafe { std::mem::transmute(k) };
    unsafe { _mm_crc32_u64(_mm_crc32_u64(0, w[0]), w[1]) as usize }
}

fn same(a: __m256i, b: __m256i) -> bool {
    unsafe { _mm256_movemask_epi8(_mm256_cmpeq_epi8(a, b)) == -1 }
}

/// Tokens of up to SHORT bytes are looked up in a hash table of 16-byte
/// entries: the key's 12 bytes and the id + 1 (0 if free), so a hit reads
/// one cache line. The few longer terms are searched linearly.
const SHORT: usize = 12;

/// Query terms to dense ids. A token that is no term gets the id one past
/// the last term.
struct Terms {
    tab: Vec<__m128i>,
    long: Vec<(__m256i, u32)>,
    n: u32,
    ring: [(__m128i, usize, usize); 8], // short terms waiting for their slot: key, hash, place in the ids
    fed: usize,
}

impl Terms {
    fn id(e: __m128i) -> u32 {
        unsafe { _mm_extract_epi32::<3>(e) as u32 }
    }

    /// Appends the id of term to ids; a short one's place is filled 8 terms
    /// later, once its slot has been prefetched, and by finish at the latest.
    fn push(&mut self, term: &[u8], ids: &mut Vec<u32>) {
        ids.push(0);
        let k = unsafe { key(term.as_ptr(), term.len()) };
        if term.len() > SHORT {
            *ids.last_mut().unwrap() = self.insert(k);
            return;
        }
        let k = unsafe { _mm256_castsi256_si128(k) };
        let h = hash(k);
        unsafe { _mm_prefetch::<_MM_HINT_T0>(self.tab.as_ptr().add(h & (self.tab.len() - 1)).cast()) };
        let oldest = std::mem::replace(&mut self.ring[self.fed % 8], (k, h, ids.len() - 1));
        if self.fed >= 8 {
            ids[oldest.2] = self.add(oldest.0, oldest.1);
        }
        self.fed += 1;
    }

    fn finish(&mut self, ids: &mut [u32]) {
        for i in self.fed.saturating_sub(8)..self.fed {
            let r = self.ring[i % 8];
            ids[r.2] = self.add(r.0, r.1);
        }
    }

    /// The place of the short key k: its entry, or the free entry it belongs in.
    fn probe(&self, k: __m128i, h: usize) -> usize {
        let mask = self.tab.len() - 1;
        let mut i = h & mask;
        loop {
            let e = self.tab[i];
            if unsafe { _mm_movemask_epi8(_mm_cmpeq_epi8(e, k)) & 0xFFF == 0xFFF } || Self::id(e) == 0 {
                return i;
            }
            i = (i + 1) & mask;
        }
    }

    fn find(&self, k: __m128i, h: usize) -> u32 {
        // The C version is byte-exact on the same 16-byte table and query key.
        // Its out-of-line foreign call is an explicit cost of this hybrid
        // implementation; results do not imply an equivalent all-C port.
        unsafe {
            track_b_find_c(
                self.tab.as_ptr().cast(),
                self.tab.len() - 1,
                (&k as *const __m128i).cast(),
                h,
                self.n,
            )
        }
    }

    fn find_long(&self, k: __m256i) -> u32 {
        self.long.iter().find(|l| same(l.0, k)).map_or(self.n, |l| l.1)
    }

    /// The id of the token of len (at most 32) bytes at p.
    unsafe fn lookup(&self, p: *const u8, len: usize) -> u32 {
        let k = key(p, len);
        let k128 = _mm256_castsi256_si128(k);
        if len > SHORT { self.find_long(k) } else { self.find(k128, hash(k128)) }
    }

    /// Term i becomes new[i].
    fn renumber(&mut self, new: &[u32]) {
        for e in &mut self.tab {
            let w: &mut [u32; 4] = unsafe { std::mem::transmute(e) };
            w[3] = if w[3] == 0 { 0 } else { new[w[3] as usize - 1] + 1 };
        }
        self.long.iter_mut().for_each(|l| l.1 = new[l.1 as usize]);
    }

    fn insert(&mut self, k: __m256i) -> u32 {
        if let Some(l) = self.long.iter().find(|l| same(l.0, k)) {
            return l.1;
        }
        self.long.push((k, self.n));
        self.n += 1;
        self.n - 1
    }

    fn add(&mut self, k: __m128i, h: usize) -> u32 {
        let i = self.probe(k, h);
        if Self::id(self.tab[i]) != 0 {
            return Self::id(self.tab[i]) - 1;
        }
        self.tab[i] = unsafe { _mm_insert_epi32::<3>(k, self.n as i32 + 1) };
        self.n += 1;
        self.n - 1
    }
}

/// Bits of the 64 bytes at p: token bytes ([A-Za-z0-9]; after OR-ing 0x20 a
/// letter lies in 'a'..='z', and a digit lies in '0'..='9' as is), and LFs.
unsafe fn classes(p: *const u8) -> (u64, u64) {
    let half = |q: *const u8| {
        let (v, c) = (_mm256_loadu_si256(q.cast()), |b: u8| _mm256_set1_epi8(b as i8));
        let at_most = |x: __m256i, n: u8| _mm256_cmpeq_epi8(_mm256_subs_epu8(x, c(n)), c(0));
        let letter = _mm256_sub_epi8(_mm256_or_si256(v, c(0x20)), c(b'a'));
        let word = _mm256_or_si256(at_most(letter, 25), at_most(_mm256_sub_epi8(v, c(b'0')), 9));
        let mask = |x: __m256i| _mm256_movemask_epi8(x) as u32 as u64;
        (mask(word), mask(_mm256_cmpeq_epi8(v, c(b'\n'))))
    };
    let ((w0, n0), (w1, n1)) = (half(p), half(p.add(32)));
    (w0 | w1 << 32, n0 | n1 << 32)
}

/// Calls f(id, start, len) for each token of the document text from p on that
/// is a term, and returns where the next line starts. Token bounds come from
/// one class mask per 64-byte window; a token that runs into the window's end
/// starts the next window. Each token's slot is prefetched and the token
/// looked up LAG tokens later, so the misses overlap.
unsafe fn tokens(mut p: *const u8, t: &Terms, mut f: impl FnMut(u32, *const u8, usize)) -> *const u8 {
    const LAG: usize = 8;
    let mut ring = [(_mm_setzero_si128(), 0u32, 0u32, p); LAG];
    let mut look = |r: &(__m128i, u32, u32, *const u8)| f(if r.2 as usize > SHORT { t.lookup(r.3, r.2 as usize) } else { t.find(r.0, r.1 as usize) }, r.3, r.2 as usize);
    let mut n = 0usize;
    loop {
        let (word, nl) = classes(p);
        let stop = if nl != 0 { nl.trailing_zeros() } else { 64 }; // the window's bytes in this document
        let mut m = word & ((1u128 << stop) - 1) as u64;
        let mut next = 64;
        while m != 0 {
            let s = m.trailing_zeros();
            let len = (!(m >> s)).trailing_zeros();
            if s + len == 64 && nl == 0 {
                next = s; // the token may go on: start the next window at it
                if s == 0 {
                    while WORD[*p.add(next as usize) as usize] {
                        next += 1; // 64 bytes or more: never a term
                    }
                }
                break;
            }
            m &= !0u64 << (s + len); // here the token ends inside the window
            if len <= 32 {
                let k = _mm256_castsi256_si128(key(p.add(s as usize), len as usize));
                let h = hash(k);
                let slot = t.tab.as_ptr().add(h & (t.tab.len() - 1));
                _mm_prefetch::<_MM_HINT_T0>(slot.cast());
                if n >= LAG {
                    look(&ring[n % LAG]);
                }
                (ring[n % LAG], n) = ((k, h as u32, len, p.add(s as usize)), n + 1);
            }
        }
        if nl != 0 {
            (n.saturating_sub(LAG)..n).for_each(|i| look(&ring[i % LAG]));
            return p.add(stop as usize + 1);
        }
        p = p.add(next as usize);
    }
}

/// With phrase[j] at text [s, s + len), do the tokens before and after it
/// spell the rest of the phrase? The document's TAB and LF bound the search.
#[inline(never)] // rare (anchor tokens only); inlined it keeps the per-token code out of line
unsafe fn around(terms: &Terms, s: *const u8, len: usize, phrase: &[u32], j: usize) -> bool {
    let is = |p: *const u8, q: *const u8, id: u32| {
        let n = q.offset_from(p) as usize;
        n <= 32 && terms.lookup(p, n) == id
    };
    let mut q = s;
    for &id in phrase[..j].iter().rev() {
        while !WORD[*q.sub(1) as usize] {
            if *q.sub(1) == b'\t' {
                return false;
            }
            q = q.sub(1);
        }
        let end = q;
        while WORD[*q.sub(1) as usize] {
            q = q.sub(1);
        }
        if !is(q, end, id) {
            return false;
        }
    }
    q = s.add(len);
    for &id in &phrase[j + 1..] {
        while !WORD[*q as usize] {
            if *q == b'\n' {
                return false;
            }
            q = q.add(1);
        }
        let start = q;
        while WORD[*q as usize] {
            q = q.add(1);
        }
        if !is(start, q, id) {
            return false;
        }
    }
    true
}

/// A set of documents: a sorted list, or a bitmap. A term's bitmap is
/// borrowed, and copied only when an operator first writes to it.
enum Docs<'a> {
    List(Vec<u32>),
    Bits(Cow<'a, [u64]>),
}

fn has(bits: &[u64], d: u32) -> bool {
    bits[d as usize / 64] >> (d % 64) & 1 != 0
}

/// cur op term, strictly left to right; a list OR a bitmap becomes a bitmap.
fn apply<'a>(cur: Docs<'a>, op: u8, term: Docs<'a>, scratch: &mut [u64]) -> Docs<'a> {
    use Docs::*;
    let bit = |d: u32| (d as usize / 64, 1u64 << (d % 64));
    match (cur, op, term) {
        (Bits(mut a), _, Bits(b)) => {
            for (x, &y) in a.to_mut().iter_mut().zip(b.iter()) {
                *x = match op {
                    b'A' => *x & y,
                    b'O' => *x | y,
                    _ => *x & !y,
                };
            }
            Bits(a)
        }
        (Bits(a), b'A', List(mut b)) | (List(mut b), b'A', Bits(a)) => (b.retain(|&d| has(&a, d)), List(b)).1,
        (List(mut a), _, Bits(b)) if op == b'N' => (a.retain(|&d| !has(&b, d)), List(a)).1,
        (Bits(mut a), _, List(b)) | (List(b), _, Bits(mut a)) => {
            let x = a.to_mut();
            for (w, m) in b.into_iter().map(bit) {
                x[w] = if op == b'O' { x[w] | m } else { x[w] & !m };
            }
            Bits(a)
        }
        (List(a), b'O', List(b)) => {
            let (mut v, mut i, mut j) = (Vec::with_capacity(a.len() + b.len()), 0, 0);
            while i < a.len() && j < b.len() {
                let (x, y) = (a[i], b[j]);
                v.push(x.min(y));
                (i, j) = (i + (x <= y) as usize, j + (y <= x) as usize);
            }
            v.extend_from_slice(&a[i..]);
            v.extend_from_slice(&b[j..]);
            List(v)
        }
        // AND or NOT: a binary search per document when b is far longer.
        (List(mut a), _, List(b)) if a.len() * 8 < b.len() => {
            let mut j = 0;
            a.retain(|&d| {
                j += b[j..].partition_point(|&x| x < d);
                (j < b.len() && b[j] == d) == (op == b'A')
            });
            List(a)
        }
        // Otherwise b's documents are marked in a scratch bitmap, which a's
        // documents are tested against; the bitmap is zeroed again at the end.
        (List(mut a), _, List(b)) => {
            for &d in &b {
                scratch[d as usize / 64] |= 1 << (d % 64);
            }
            a.retain(|&d| has(scratch, d) == (op == b'A'));
            for &d in &b {
                scratch[d as usize / 64] = 0;
            }
            List(a)
        }
    }
}

/// "count sum id1 ... id10" for a set of documents.
fn line(out: &mut String, docs: &Docs) {
    let (mut count, mut sum, mut first) = (0u64, 0u64, Vec::with_capacity(10));
    match docs {
        Docs::List(v) => {
            (count, sum) = (v.len() as u64, v.iter().map(|&d| d as u64).sum());
            first.extend(v.iter().take(10));
        }
        Docs::Bits(b) => {
            // Bit k of a position adds 2^k for each set bit whose position has it.
            const SEL: [u64; 6] = [0xAAAA_AAAA_AAAA_AAAA, 0xCCCC_CCCC_CCCC_CCCC, 0xF0F0_F0F0_F0F0_F0F0, 0xFF00_FF00_FF00_FF00, 0xFFFF_0000_FFFF_0000, 0xFFFF_FFFF_0000_0000];
            for (w, &x) in b.iter().enumerate().filter(|(_, &x)| x != 0) {
                let c = x.count_ones() as u64;
                let within: u64 = (0..6).map(|k| ((x & SEL[k]).count_ones() as u64) << k).sum();
                (count, sum) = (count + c, sum + c * 64 * w as u64 + within);
                let mut m = x;
                while m != 0 && first.len() < 10 {
                    first.push(w as u32 * 64 + m.trailing_zeros());
                    m &= m - 1;
                }
            }
        }
    }
    write!(out, "{count} {sum}").unwrap();
    first.iter().for_each(|d| write!(out, " {d}").unwrap());
    out.push('\n');
}

/// Calls f(doc, id, start, len) for the term tokens of the lines in text,
/// the first being document first; returns the number of documents.
fn walk(text: &[u8], first: u32, t: &Terms, mut f: impl FnMut(u32, u32, *const u8, usize)) -> u32 {
    let (mut p, end, mut doc) = (text.as_ptr(), text.as_ptr_range().end, first);
    while p < end {
        unsafe {
            while *p != b'\t' {
                p = p.add(1);
            }
            p = tokens(p, t, #[inline(always)] |id, s, len| f(doc, id, s, len));
        }
        doc += 1;
    }
    doc - first
}

/// Scan output for a bucket of terms, in 64 KB chunks so that only the last
/// one is ever part empty. A unit is a term's index in the bucket (i / BUCKETS)
/// above the gap since the bucket's previous document; a thread meets its
/// documents in order, so the gaps are small.
#[derive(Default)]
struct Units {
    chunks: Vec<usize>, // addresses of the mapped chunks
    at: usize, // the address of the next unit's place in the last chunk, up to end
    end: usize,
    prev: u32,
}

impl Units {
    fn put(&mut self, x: u16) {
        if self.at == self.end {
            self.grow();
        }
        unsafe { *(self.at as *mut u16) = x };
        self.at += 2;
    }

    #[cold]
    #[inline(never)]
    fn grow(&mut self) {
        // Chunks are cut from a mapping made per thread, so that the threads
        // rarely take the mapping lock; unused pages of it cost nothing.
        thread_local!(static ARENA: std::cell::Cell<(usize, usize)> = const { std::cell::Cell::new((0, 0)) });
        let (mut p, mut end) = ARENA.get();
        if p == end {
            // PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS
            p = unsafe { mmap(std::ptr::null_mut(), 64 * CHUNK, 3, 0x22, -1, 0) } as usize;
            end = p + 64 * CHUNK;
        }
        ARENA.set((p + CHUNK, end));
        (self.at, self.end) = (p, p + CHUNK);
        self.chunks.push(p);
    }

    fn add(&mut self, offset: u32, doc: u32) {
        let gap = doc - self.prev;
        self.put((offset << 4 | gap.min(GAP)) as u16);
        self.prev = doc;
        if gap >= GAP {
            self.put(gap as u16);
        }
    }
}


fn main() {
    unsafe { mallopt(-3, 1 << 16) }; // M_MMAP_THRESHOLD: freed buffers go back to the kernel
    // PR_SET_THP_DISABLE: the box backs part-written regions with whole 2 MB pages.
    unsafe { prctl(41, 1u64, 0u64, 0u64, 0u64) };
    let qtext = map("/data/queries.txt");
    let tab: Vec<__m128i> = unsafe { std::mem::transmute(vec![0u128; (qtext.len() / 8).next_power_of_two()]) };
    let mut terms = Terms { tab, long: vec![], n: 0, ring: [(unsafe { _mm_setzero_si128() }, 0, 0); 8], fed: 0 };
    // Queries, flat: query q's terms are ids[span.1..span.2], and for BOOL
    // ops[i] joins ids[i] and ids[i + 1].
    let (mut spans, mut ids, mut ops) = (vec![], vec![], vec![]);
    for q in qtext.split(|&b| b == b'\n').filter(|q| !q.is_empty()) {
        let (phrase, from) = (q[0] == b'P', ids.len() as u32);
        let body = &q[if phrase { 8 } else { 5 }..q.len() - phrase as usize];
        for (k, word) in body.split(|&b| b == b' ').enumerate() {
            match !phrase && k % 2 == 1 {
                true => *ops.last_mut().unwrap() = word[0],
                false => (terms.push(word, &mut ids), ops.push(0)).1,
            }
        }
        spans.push((phrase, from, ids.len() as u32));
    }
    terms.finish(&mut ids);
    let nterms = terms.n as usize;
    assert!(nterms <= BUCKETS << 12, "too many terms"); // a unit's index has 12 bits
    let mut in_bool = vec![false; nterms];
    for &(phrase, from, to) in spans.iter().filter(|s| !s.0) {
        ids[from as usize..to as usize].iter().for_each(|&id| in_bool[id as usize] = !phrase);
    }

    let text = map("/data/docs.txt");
    let mut cut: Vec<usize> = (0..=THREADS).map(|t| text.len() * t / THREADS).collect();
    for c in &mut cut[1..THREADS] {
        while *c < text.len() && text[*c - 1] != b'\n' {
            *c += 1;
        }
    }
    let mut last_line = text.len().saturating_sub(1);
    while last_line > 0 && text[last_line - 1] != b'\n' {
        last_line -= 1;
    }
    let ndocs = if text.is_empty() { 0 } else { number(text, last_line) as usize + 1 };
    let first_doc = |c: usize| text.get(c).map_or(ndocs as u32, |_| number(text, c));
    let firsts: Vec<u32> = cut[..THREADS].iter().map(|&c| first_doc(c)).collect();

    // The sample: documents per term in the first 1/64 of each share.
    let samples = on_threads(|t| {
        let (mut df, mut last) = (vec![0u32; nterms + 1], vec![NONE; nterms + 1]);
        let sample = &text[cut[t]..cut[t] + (cut[t + 1] - cut[t]) / 64];
        let n = walk(sample, firsts[t], &terms, |doc, id, _, _| {
            if std::mem::replace(&mut last[id as usize], doc) != doc {
                df[id as usize] += 1;
            }
        });
        (n as usize, df)
    });
    let sampled: usize = samples.iter().map(|s| s.0).sum();
    let mut df = vec![0u32; nterms + 1];
    for (_, d) in samples {
        df.iter_mut().zip(d).for_each(|(a, b)| *a += b);
    }
    // Each phrase's anchor, its rarest term: (anchor, query, position).
    let (mut anchored, mut anchor) = (vec![], vec![false; nterms]);
    for (q, &(_, from, to)) in spans.iter().enumerate().filter(|s| s.1 .0) {
        let j = (from..to).min_by_key(|&j| df[ids[j as usize] as usize]).unwrap();
        anchored.push((ids[j as usize], q as u32, j - from));
        anchor[ids[j as usize] as usize] = true;
    }
    // Terms renumbered by class, so the scan reads a token's role off its id:
    // bitmaps, lists, lists that anchor phrases, anchors only, and terms only
    // phrases read (their tokens need no state). Within a class the most
    // frequent come first, so the state of common terms shares cache lines.
    let class = |i: usize| match (in_bool[i], anchor[i]) {
        (true, false) => (df[i] as usize * 32 <= sampled) as usize,
        (true, true) => 2,
        (false, true) => 3,
        (false, false) => 4,
    };
    // Sorted by class, then by falling frequency, as the term's id in the low bits.
    let mut order: Vec<u64> = (0..nterms).map(|i| (class(i) as u64) << 56 | (!df[i] as u64) << 24 | i as u64).collect();
    order.sort_unstable();
    let [nbits, a0, lists, nkeep] = [0, 1, 2, 3].map(|c| order.partition_point(|&k| k >> 56 <= c));
    let mut new = vec![0; nterms];
    for (r, &k) in order.iter().enumerate() {
        new[k as usize & 0xFF_FFFF] = r as u32;
    }
    terms.renumber(&new);
    ids.iter_mut().for_each(|id| *id = new[*id as usize]);
    anchored.iter_mut().for_each(|a| a.0 = new[a.0 as usize]);
    anchored.sort_unstable();
    // The phrases of anchor a0 + k lie at recs[astart[k]..astart[k + 1]], each as
    // its query, the anchor's position, its length and its terms.
    let (mut recs, mut astart) = (vec![], vec![]);
    for &(a, q, j) in &anchored {
        astart.resize(a as usize - a0 + 1, recs.len());
        let span = spans[q as usize];
        recs.extend([q, j, span.2 - span.1]);
        recs.extend_from_slice(&ids[span.1 as usize..span.2 as usize]);
    }
    astart.resize(nkeep - a0 + 1, recs.len());
    drop((df, order, new, in_bool, anchor, anchored));
    let words = ndocs.div_ceil(64);
    // Zero pages from the kernel, written only where a bit is set.
    let bits: Vec<AtomicU64> = unsafe { std::mem::transmute(vec![0u64; nbits * words]) };

    // The scan: phrase matches, bitmap bits, and list postings by bucket.
    let found = Mutex::new(vec![]); // (query, document) phrase matches
    let mut scans: Vec<(Vec<u16>, Mutex<Vec<Units>>)> = on_threads(|t| {
        // Per term (and one id for every token that needs nothing): documents
        // in this share, and the last one's offset + 1.
        let (mut count, mut last, mut mine) = (vec![0u16; nkeep + 1], vec![0u16; nkeep + 1], vec![]);
        let mut buckets: Vec<Units> = (0..BUCKETS).map(|_| Units::default()).collect();
        let first = firsts[t];
        // Each bitmap term's bits for the current 64 documents (one more word
        // takes the bits of every other term), ORed in when the block ends.
        let (mut pending, mut block) = (vec![0u64; nbits + 1], first as usize / 64);
        let flush = |pending: &mut [u64], w: usize| {
            for (b, x) in pending[..nbits].iter_mut().enumerate().filter(|(_, x)| **x != 0) {
                bits[b * words + w].fetch_or(std::mem::take(x), Relaxed);
            }
        };
        // Bitmap OR is idempotent; only list postings need per-document
        // deduplication. Unused terms and phrase-only anchors need no posting.
        let n = walk(&text[cut[t]..cut[t + 1]], first, &terms, #[inline(always)] |doc, id, s, len| {
            let i = (id as usize).min(nkeep);
            if i == nkeep {
                return;
            }
            if i.wrapping_sub(a0) < nkeep - a0 {
                let k = i - a0;
                let mut at = &recs[astart[k]..astart[k + 1]];
                while let [q, j, n, rest @ ..] = at {
                    let (phrase, after) = rest.split_at(*n as usize);
                    if unsafe { around(&terms, s, len, phrase, *j as usize) } {
                        mine.push((*q, doc));
                    }
                    at = after;
                }
            }
            if i < nbits {
                if doc as usize / 64 != block {
                    let w = std::mem::replace(&mut block, doc as usize / 64);
                    flush(&mut pending, w);
                }
                pending[i] |= 1u64 << (doc % 64);
                return;
            }
            if i >= lists {
                return;
            }
            let mark = (doc - first + 1) as u16;
            let new = std::mem::replace(&mut last[i], mark) != mark;
            if new {
                buckets[i % BUCKETS].add((i / BUCKETS) as u32, doc - first);
                count[i] += 1;
            }
        });
        flush(&mut pending, block);
        assert!(n < 1 << DOC_BITS, "too many documents per thread");
        found.lock().unwrap().extend(mine);
        (count, Mutex::new(buckets))
    });
    drop((terms, recs, astart));
    // Each term's postings lie in one stretch, thread by thread: the segment
    // from thread t holds seg[i * THREADS + t] offsets from firsts[t]. The
    // stretches go bucket by bucket, so a bucket's scatter writes one region,
    // and frequency ranks dealt round-robin keep the regions small.
    let (mut seg, mut start, mut end) = (vec![0u16; lists * THREADS], vec![0u32; lists], 0);
    for i in (0..BUCKETS).flat_map(|b| (b..lists).step_by(BUCKETS)) {
        let s = &mut seg[i * THREADS..][..THREADS];
        s.iter_mut().zip(&scans).for_each(|(n, scan)| *n = scan.0[i]);
        (start[i], end) = (end, end + s.iter().map(|&n| n as u32).sum::<u32>());
    }
    scans.iter_mut().for_each(|s| s.0 = vec![]);
    // Written by every thread at its own positions; relaxed stores are plain stores.
    let post: Vec<AtomicU16> = unsafe { std::mem::transmute(vec![0u16; end as usize]) };
    on_threads(|t| {
        let buckets = std::mem::take(&mut *scans[t].1.lock().unwrap());
        let mut cursor = vec![0u32; lists.div_ceil(BUCKETS)];
        for (b, units) in buckets.into_iter().enumerate() {
            for (c, i) in cursor.iter_mut().zip((b..lists).step_by(BUCKETS)) {
                *c = start[i] + seg[i * THREADS..][..t].iter().map(|&n| n as u32).sum::<u32>();
            }
            let (mut doc, mut term) = (0u32, NONE);
            for (k, &p) in units.chunks.iter().enumerate() {
                let used = if k + 1 == units.chunks.len() { units.at - p } else { CHUNK };
                for &x in unsafe { std::slice::from_raw_parts(p as *const u16, used / 2) } {
                    if term == NONE {
                        term = (x >> 4) as u32;
                        if x as u32 & GAP == GAP {
                            continue; // the whole gap follows
                        }
                        doc += x as u32 & GAP;
                    } else {
                        doc += x as u32;
                    }
                    let c = &mut cursor[std::mem::replace(&mut term, NONE) as usize];
                    (post[*c as usize].store(doc as u16, Relaxed), *c += 1);
                }
                unsafe { madvise(p as *mut u8, CHUNK, 4) }; // MADV_DONTNEED: no unmapping while the other threads fault
            }
        }
    });
    drop(scans);
    let post: Vec<u16> = unsafe { std::mem::transmute(post) };
    let bits: Vec<u64> = unsafe { std::mem::transmute(bits) };
    let mut found = found.into_inner().unwrap();
    found.sort_unstable();
    found.dedup();

    // Answers, 64 queries at a time on every thread. Large lists come and go
    // here, so they now reuse heap memory instead of a fresh mapping each.
    unsafe { mallopt(-3, 64 << 20) };
    let docs = |id: u32| match id as usize {
        i if i < nbits => Docs::Bits(Cow::Borrowed(&bits[i * words..(i + 1) * words])),
        i => {
            let mut at = start[i] as usize;
            let segs = &seg[i * THREADS..][..THREADS];
            let mut v = Vec::with_capacity(segs.iter().map(|&n| n as usize).sum());
            for (&n, &first) in segs.iter().zip(&firsts) {
                v.extend(post[at..at + n as usize].iter().map(|&d| first + d as u32));
                at += n as usize;
            }
            Docs::List(v)
        }
    };
    let chunks: Vec<Mutex<String>> = (0..spans.len().div_ceil(64)).map(|_| Mutex::default()).collect();
    let next = AtomicUsize::new(0);
    on_threads(|_| {
        let mut scratch = vec![0u64; words]; // zero between uses
        loop {
            let k = next.fetch_add(1, Relaxed);
            let Some(chunk) = chunks.get(k) else { return };
            let mut out = String::new();
            for (q, &(phrase, from, to)) in spans.iter().enumerate().skip(k * 64).take(64) {
                let (tids, ops) = (&ids[from as usize..to as usize], &ops[from as usize..to as usize]);
                let set = if phrase {
                    let at = found.partition_point(|m| m.0 < q as u32);
                    Docs::List(found[at..].iter().take_while(|m| m.0 == q as u32).map(|m| m.1).collect())
                } else {
                    let cur = docs(tids[0]);
                    ops.iter().zip(&tids[1..]).fold(cur, |cur, (&op, &id)| match &cur {
                        Docs::List(v) if v.is_empty() && op != b'O' => cur, // nothing left to narrow
                        _ => apply(cur, op, docs(id), &mut scratch),
                    })
                };
                line(&mut out, &set);
            }
            *chunk.lock().unwrap() = out;
        }
    });
    let out: String = chunks.into_iter().map(|c| c.into_inner().unwrap()).collect();
    std::fs::write("/out/results.txt", out).expect("/out/results.txt");
    std::process::exit(0); // the big buffers go with the process, not one unmapping at a time
}
