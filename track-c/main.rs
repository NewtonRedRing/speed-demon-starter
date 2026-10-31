use std::collections::HashMap;
use std::fs;
use std::io::{self, Write};
use std::os::fd::AsRawFd;
use std::ops::Deref;
use std::ptr;
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::Arc;
use std::thread;
use std::arch::x86_64::_mm_crc32_u8;

unsafe extern "C" {
    fn mmap(a: *mut u8, n: usize, p: i32, f: i32, fd: i32, o: isize) -> *mut u8;
    fn munmap(a: *mut u8, n: usize) -> i32;
    fn prctl(option: i32, arg2: usize, arg3: usize, arg4: usize, arg5: usize) -> i32;
}

struct Mapping { address: usize, len: usize }
impl Mapping {
    fn open(path: &str) -> io::Result<Self> {
        let f = fs::File::open(path)?;
        let len = f.metadata()?.len() as usize;
        let p = unsafe { mmap(ptr::null_mut(), len, 1, 2, f.as_raw_fd(), 0) };
        if p as isize == -1 { return Err(io::Error::last_os_error()); }
        Ok(Self { address: p as usize, len })
    }
}
impl Deref for Mapping {
    type Target = [u8];
    fn deref(&self) -> &[u8] { unsafe { std::slice::from_raw_parts(self.address as *const u8, self.len) } }
}
impl Drop for Mapping {
    fn drop(&mut self) { unsafe { munmap(self.address as *mut u8, self.len); } }
}

#[derive(Clone, Copy)]
struct Aggregate {
    count: u64, id_sum: u64, sum: i64, min: i64, max: i64,
    distinct: u64, name_bytes: u64, note_bytes: u64,
}
impl Default for Aggregate {
    fn default() -> Self {
        Self { count: 0, id_sum: 0, sum: 0, min: i64::MAX, max: i64::MIN,
               distinct: 0, name_bytes: 0, note_bytes: 0 }
    }
}

#[inline(always)]
fn number(data: &[u8], p: &mut usize) -> u64 {
    let mut v = 0;
    while *p < data.len() && data[*p].is_ascii_digit() {
        v = v * 10 + (data[*p] - b'0') as u64;
        *p += 1;
    }
    v
}
#[inline(always)]
fn skip_bom(data: &[u8], p: &mut usize) {
    if data.get(*p..*p + 3) == Some(&[0xef, 0xbb, 0xbf]) { *p += 3; }
}

#[inline(always)]
fn name_field(data: &[u8], p: &mut usize) -> (u32, usize, u64) {
    let start = *p;
    let mut n = 0usize;
    let mut h = 0x9e37_79b9u64;
    if data[*p] != b'"' {
        while data[*p] != b',' {
            h = unsafe { _mm_crc32_u8(h as u32, data[*p]) as u64 };
            n += 1; *p += 1;
        }
        *p += 1;
    } else {
        *p += 1;
        loop {
            let c = data[*p]; *p += 1;
            if c == b'"' {
                if data[*p] == b'"' {
                    h = unsafe { _mm_crc32_u8(h as u32, b'"') as u64 };
                    n += 1; *p += 1;
                } else { break; }
            } else {
                h = unsafe { _mm_crc32_u8(h as u32, c) as u64 };
                n += 1;
            }
        }
        debug_assert_eq!(data[*p], b',');
        *p += 1;
    }
    (start as u32, n, h.wrapping_mul(0x9e37_79b9_7f4a_7c15) ^ n as u64)
}

#[inline(always)]
fn text_len(data: &[u8], p: &mut usize) -> usize {
    if data[*p] != b'"' {
        let start = *p;
        while data[*p] != b',' { *p += 1; }
        let n = *p - start; *p += 1; return n;
    }
    *p += 1;
    let mut n = 0usize;
    loop {
        let c = data[*p]; *p += 1;
        if c == b'"' {
            if data[*p] == b'"' { n += 1; *p += 1; } else { break; }
        } else { n += 1; }
    }
    debug_assert_eq!(data[*p], b',');
    *p += 1;
    n
}

struct ValueIter<'a> { data: &'a [u8], p: usize, quoted: bool, done: bool }
impl<'a> ValueIter<'a> {
    #[inline(always)] fn new(data: &'a [u8], off: usize) -> Self {
        let quoted = data[off] == b'"';
        Self { data, p: off + quoted as usize, quoted, done: false }
    }
    #[inline(always)] fn next_byte(&mut self) -> Option<u8> {
        if self.done { return None; }
        let c = self.data[self.p];
        if !self.quoted {
            if c == b',' { self.done = true; return None; }
            self.p += 1; return Some(c);
        }
        self.p += 1;
        if c != b'"' { return Some(c); }
        if self.data[self.p] == b'"' { self.p += 1; Some(b'"') }
        else { self.done = true; None }
    }
}

#[inline(always)]
fn same_value(data: &[u8], a: usize, b: usize) -> bool {
    let mut x = ValueIter::new(data, a);
    let mut y = ValueIter::new(data, b);
    loop {
        match (x.next_byte(), y.next_byte()) {
            (Some(a), Some(b)) if a == b => {}
            (None, None) => return true,
            _ => return false,
        }
    }
}

struct NameSlot { meta: AtomicU64, countries: [AtomicU64; 4] }
struct NameTable { slots: Box<[NameSlot]> }
impl NameTable {
    fn new() -> Self {
        let slots = (0..131_072).map(|_| NameSlot {
            meta: AtomicU64::new(0),
            countries: std::array::from_fn(|_| AtomicU64::new(0)),
        }).collect::<Vec<_>>().into_boxed_slice();
        Self { slots }
    }
    #[inline(always)]
    fn mark(&self, data: &[u8], off: u32, hash: u64, country: usize) -> bool {
        let mask = self.slots.len() - 1;
        let tag = (hash >> 32) as u32;
        let candidate = ((tag as u64) << 32) | (off as u64 + 1);
        let mut i = hash as usize & mask;
        loop {
            let slot = &self.slots[i];
            let mut meta = slot.meta.load(Ordering::Acquire);
            if meta == 0 {
                match slot.meta.compare_exchange(0, candidate, Ordering::AcqRel, Ordering::Acquire) {
                    Ok(_) => meta = candidate,
                    Err(actual) => meta = actual,
                }
            }
            if (meta >> 32) as u32 == tag {
                let stored = (meta as u32).wrapping_sub(1) as usize;
                if same_value(data, off as usize, stored) {
                    let word = country >> 6;
                    let bit = 1u64 << (country & 63);
                    return slot.countries[word].fetch_or(bit, Ordering::Relaxed) & bit == 0;
                }
            }
            i = (i + 1) & mask;
        }
    }
}

fn main() -> io::Result<()> {
    unsafe { prctl(41, 1, 0, 0, 0); }
    let regions_data = fs::read("/data/regions.csv")?;
    let mut p = b"region_id,country\n".len();
    let mut country_ids: HashMap<[u8; 3], u8> = HashMap::new();
    let mut countries = vec!["_UNMATCHED".to_string()];
    let mut regions = Vec::<u8>::new();
    while p < regions_data.len() {
        let r = number(&regions_data, &mut p) as usize; p += 1;
        let code: [u8; 3] = regions_data[p..p + 3].try_into().unwrap(); p += 4;
        let id = *country_ids.entry(code).or_insert_with(|| {
            countries.push(String::from_utf8(code.to_vec()).unwrap());
            (countries.len() - 1) as u8
        });
        if r >= regions.len() { regions.resize(r + 1, 0); }
        regions[r] = id;
    }

    let data = Arc::new(Mapping::open("/data/records.csv")?);
    p = 0; skip_bom(&data, &mut p);
    let header = b"id,name,note,region_id,amount";
    assert_eq!(&data[p..p + header.len()], header);
    p += header.len(); if data[p] == b'\r' { p += 1; } p += 1;

    let workers = if data.len() < 1 << 20 { 1 } else { 12 };
    let parity: Vec<_> = (0..workers).map(|_| AtomicBool::new(false)).collect();
    thread::scope(|scope| for w in 0..workers - 1 {
        let input = &data; let slot = &parity[w];
        scope.spawn(move || {
            let a = input.len() * w / workers;
            let b = input.len() * (w + 1) / workers;
            slot.store(input[a..b].iter().filter(|&&c| c == b'"').count() & 1 == 1, Ordering::Relaxed);
        });
    });
    let mut bounds = vec![0usize; workers + 1]; bounds[0] = p; bounds[workers] = data.len();
    let mut quoted = false;
    for w in 1..workers {
        quoted ^= parity[w - 1].load(Ordering::Relaxed);
        let mut q = data.len() * w / workers; let mut inside = quoted;
        while q < data.len() {
            let c = data[q]; q += 1;
            if c == b'"' { inside = !inside; }
            if c == b'\n' && !inside { break; }
        }
        bounds[w] = q;
    }

    let table = Arc::new(NameTable::new());
    let regions = Arc::new(regions);
    let mut joins = Vec::new();
    for w in 0..workers {
        let input = data.clone(); let names = table.clone(); let rmap = regions.clone();
        let begin = bounds[w]; let end = bounds[w + 1]; let count = countries.len();
        joins.push(thread::spawn(move || {
            let mut a = vec![Aggregate::default(); count];
            let mut p = begin;
            while p < end {
                skip_bom(&input, &mut p);
                let id = number(&input, &mut p); p += 1;
                let (name_off, nb, name_hash) = name_field(&input, &mut p);
                let tb = text_len(&input, &mut p);
                let region = number(&input, &mut p) as usize; p += 1;
                let neg = input[p] == b'-'; if neg { p += 1; }
                let whole = number(&input, &mut p) as i64; p += 1;
                let cents = whole * 100 + ((input[p] - b'0') as i64) * 10 + (input[p + 1] - b'0') as i64;
                p += 2; if p < input.len() && input[p] == b'\r' { p += 1; } if p < input.len() { p += 1; }
                let country = if region < rmap.len() { rmap[region] as usize } else { 0 };
                let value = if neg { -cents } else { cents };
                let x = &mut a[country];
                x.count += 1; x.id_sum += id; x.sum += value; x.min = x.min.min(value); x.max = x.max.max(value);
                x.name_bytes += nb as u64; x.note_bytes += tb as u64;
                if names.mark(&input, name_off, name_hash, country) { x.distinct += 1; }
            }
            a
        }));
    }

    let mut total = vec![Aggregate::default(); countries.len()];
    for j in joins {
        let local = j.join().unwrap();
        for (i, s) in local.into_iter().enumerate() {
            let t = &mut total[i]; t.count += s.count; t.id_sum += s.id_sum; t.sum += s.sum;
            t.min = t.min.min(s.min); t.max = t.max.max(s.max); t.distinct += s.distinct;
            t.name_bytes += s.name_bytes; t.note_bytes += s.note_bytes;
        }
    }
    let mut order: Vec<_> = (0..countries.len()).collect(); order.sort_unstable_by_key(|&i| &countries[i]);
    let money = |v: i64| { let n = v.unsigned_abs(); format!("{}{}.{:02}", if v < 0 { "-" } else { "" }, n / 100, n % 100) };
    let mut out = Vec::new();
    for i in order {
        let a = total[i]; if a.count == 0 { continue; }
        writeln!(out, "{} {} {} {} {} {} {} {} {}", countries[i], a.count, a.id_sum,
                 money(a.sum), money(a.min), money(a.max), a.distinct, a.name_bytes, a.note_bytes)?;
    }
    fs::write("/out/aggregates.txt", out)
}
