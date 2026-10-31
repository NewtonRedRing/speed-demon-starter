use std::cmp::min;
use std::fs;
use std::io;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Arc;
use std::sync::atomic::AtomicI64;
use std::thread;
use std::os::fd::AsRawFd;use std::ops::Deref;use std::ptr;
unsafe extern "C"{fn mmap(a:*mut u8,n:usize,p:i32,f:i32,fd:i32,o:isize)->*mut u8;fn munmap(a:*mut u8,n:usize)->i32;}
struct Mapping{address:usize,len:usize}impl Mapping{fn open(path:&str)->io::Result<Self>{let f=fs::File::open(path)?;let len=f.metadata()?.len()as usize;let p=unsafe{mmap(ptr::null_mut(),len,1,2,f.as_raw_fd(),0)};if p as isize == -1{return Err(io::Error::last_os_error())}Ok(Self{address:p as usize,len})}}impl Deref for Mapping{type Target=[u8];fn deref(&self)->&[u8]{unsafe{std::slice::from_raw_parts(self.address as*const u8,self.len)}}}impl Drop for Mapping{fn drop(&mut self){unsafe{munmap(self.address as*mut u8,self.len);}}}

const INF: u64 = u64::MAX;

#[inline(always)]
fn number(data: &[u8], p: &mut usize) -> u32 {
    let mut value = 0u32;
    while *p < data.len() {
        let c = data[*p];
        if !c.is_ascii_digit() { break; }
        value = value * 10 + (c - b'0') as u32;
        *p += 1;
    }
    value
}

#[derive(Clone, Copy)]
struct Item { key: u64, node: u32 }

struct RadixHeap {
    buckets: [Vec<Item>; 65],
    last: u64,
    len: usize,
}
impl RadixHeap {
    fn new() -> Self { Self { buckets: std::array::from_fn(|_| Vec::new()), last: 0, len: 0 } }
    fn clear(&mut self) { for b in &mut self.buckets { b.clear(); } self.last = 0; self.len = 0; }
    #[inline(always)] fn push(&mut self, key: u64, node: u32) {
        let bucket = (64 - (key ^ self.last).leading_zeros()) as usize;
        self.buckets[bucket].push(Item { key, node }); self.len += 1;
    }
    #[inline(always)] fn top(&mut self) -> Option<Item> {
        if self.len == 0 { return None; }
        if self.buckets[0].is_empty() {
            let b = (1..65).find(|&i| !self.buckets[i].is_empty()).unwrap();
            self.last = self.buckets[b].iter().map(|x| x.key).min().unwrap();
            let moved = std::mem::take(&mut self.buckets[b]);
            for item in moved {
                let to = (64 - (item.key ^ self.last).leading_zeros()) as usize;
                self.buckets[to].push(item);
            }
        }
        self.buckets[0].last().copied()
    }
    #[inline(always)] fn pop(&mut self) { self.buckets[0].pop(); self.len -= 1; }
}

#[derive(Clone, Copy, Default)]
struct Slot { key: u32, stamp: u32, value: u64 }
struct Distances { slots: Vec<Slot>, stamp: u32, count: usize }
impl Distances {
    fn new() -> Self { Self { slots: vec![Slot::default(); 2048], stamp: 1, count: 0 } }
    fn clear(&mut self) { self.count = 0; self.stamp += 1; if self.stamp == 0 { self.slots.fill(Slot::default()); self.stamp = 1; } }
    #[inline(always)] fn hash(key: u32) -> usize {
        let x = key as u64 * 0x9e3779b97f4a7c15; (x ^ (x >> 32)) as usize
    }
    #[inline(always)] fn get(&self, key: u32) -> u64 {
        let mut i = Self::hash(key) & (self.slots.len() - 1);
        loop { let s = self.slots[i]; if s.stamp != self.stamp { return INF; } if s.key == key { return s.value; } i = (i + 1) & (self.slots.len() - 1); }
    }
    fn relax(&mut self, key: u32, value: u64) -> bool {
        let mut i = Self::hash(key) & (self.slots.len() - 1);
        loop {
            let s = self.slots[i];
            if s.stamp != self.stamp { self.slots[i] = Slot { key, stamp: self.stamp, value }; self.count += 1; if self.count * 2 > self.slots.len() { self.grow(); } return true; }
            if s.key == key { if value >= s.value { return false; } self.slots[i].value = value; return true; }
            i = (i + 1) & (self.slots.len() - 1);
        }
    }
    fn grow(&mut self) {
        let capacity = self.slots.len() * 2;
        let old = std::mem::replace(&mut self.slots, vec![Slot::default(); capacity]);
        self.count = 0;
        for s in old { if s.stamp == self.stamp { self.relax(s.key, s.value); } }
    }
}

struct Graph {
    bits: u32, mask: u32, sentinel: u32,
    offsets: [Vec<u32>; 2], edges: [Vec<u32>; 2], wide: [Vec<(u32,u32)>; 2],
}
impl Graph {
    #[inline(always)] fn edge(&self, d: usize, i: u32) -> (u32,u32) {
        let e = self.edges[d][i as usize]; let to = e & self.mask; let packed = e >> self.bits;
        if packed != self.sentinel { (to, packed) } else {
            let pos = self.wide[d].binary_search_by_key(&i, |x| x.0).unwrap(); (to, self.wide[d][pos].1)
        }
    }
}

struct Search { dist: [Distances;2], heap: [RadixHeap;2] }
impl Search {
    fn new() -> Self { Self { dist: [Distances::new(),Distances::new()], heap: [RadixHeap::new(),RadixHeap::new()] } }
    fn run(&mut self, graph: &Graph, source: u32, target: u32) -> i64 {
        if source == target { return 0; }
        if graph.offsets[0][source as usize] == graph.offsets[0][source as usize+1] || graph.offsets[1][target as usize] == graph.offsets[1][target as usize+1] { return -1; }
        for d in 0..2 { self.dist[d].clear(); self.heap[d].clear(); let n=if d==0 {source}else{target}; self.dist[d].relax(n,0); self.heap[d].push(0,n); }
        let mut best=INF;
        loop {
            for d in 0..2 { loop { let Some(x)=self.heap[d].top() else {break}; if self.dist[d].get(x.node)==x.key {break} self.heap[d].pop(); } }
            let (Some(a),Some(b))=(self.heap[0].top(),self.heap[1].top()) else {break};
            if best!=INF && a.key+b.key>=best {break}
            let d=if self.heap[0].len<=self.heap[1].len {0}else{1}; let item=self.heap[d].top().unwrap(); self.heap[d].pop();
            let start=graph.offsets[d][item.node as usize]; let stop=graph.offsets[d][item.node as usize+1];
            for i in start..stop { let (to,w)=graph.edge(d,i); let next=item.key+w as u64; if next>=best || !self.dist[d].relax(to,next){continue} let other=self.dist[1-d].get(to); if other!=INF {best=min(best,next+other)} if next<best {self.heap[d].push(next,to)} }
        }
        if best==INF {-1}else{best as i64}
    }
}

fn main() -> io::Result<()> {
    let data=Mapping::open("/data/graph.txt")?; let mut p=0; let n=number(&data,&mut p) as usize; p+=1; let m=number(&data,&mut p) as usize; p+=1;
    let header=p; let mut degree=[vec![0u32;n+1],vec![0u32;n+1]];
    for _ in 0..m { let u=number(&data,&mut p) as usize;p+=1;let v=number(&data,&mut p) as usize;p+=1;let _=number(&data,&mut p);p+=1;degree[0][u+1]+=1;degree[1][v+1]+=1; }
    for d in 0..2 { for i in 1..=n {degree[d][i]+=degree[d][i-1]} }
    let mut offsets=degree; let mut edges=[vec![0u32;m],vec![0u32;m]]; let bits=(usize::BITS-(n-1).leading_zeros()) as u32; let mask=((1u64<<bits)-1) as u32; let sentinel=u32::MAX>>bits; let mut wide=[Vec::new(),Vec::new()]; p=header;
    for _ in 0..m { let u=number(&data,&mut p);p+=1;let v=number(&data,&mut p);p+=1;let w=number(&data,&mut p);p+=1; let packed=w.min(sentinel)<<bits; let a=offsets[0][u as usize];offsets[0][u as usize]+=1;edges[0][a as usize]=v|packed;let b=offsets[1][v as usize];offsets[1][v as usize]+=1;edges[1][b as usize]=u|packed;if w>=sentinel {wide[0].push((a,w));wide[1].push((b,w));} }
    for d in 0..2{for i in(1..=n).rev(){offsets[d][i]=offsets[d][i-1]}offsets[d][0]=0}
    drop(data); for x in &mut wide {x.sort_unstable_by_key(|v|v.0)}
    let graph=Arc::new(Graph{bits,mask,sentinel,offsets,edges,wide});
    let qdata=fs::read("/data/queries.txt")?;p=0;let mut queries=Vec::with_capacity(2000);while p<qdata.len(){let a=number(&qdata,&mut p);p+=1;let b=number(&qdata,&mut p);p+=1;queries.push((a,b));} let queries=Arc::new(queries);
    let answers=Arc::new((0..queries.len()).map(|_|AtomicI64::new(0)).collect::<Vec<_>>());let next=Arc::new(AtomicUsize::new(0));let mut joins=Vec::new();
    for _ in 0..12 {let g=graph.clone();let q=queries.clone();let a=answers.clone();let nx=next.clone();joins.push(thread::spawn(move||{let mut search=Search::new();loop{let i=nx.fetch_add(1,Ordering::Relaxed);if i>=q.len(){break}a[i].store(search.run(&g,q[i].0,q[i].1),Ordering::Relaxed);}}));}
    for j in joins {j.join().unwrap()} let mut out=Vec::with_capacity(answers.len()*12);for value in answers.iter(){use std::io::Write;writeln!(out,"{}",value.load(Ordering::Relaxed))?;}fs::write("/out/distances.txt",out)
}
