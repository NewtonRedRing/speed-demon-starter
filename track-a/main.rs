use std::collections::HashMap;
use std::fs;
use std::io::{self, Write};
use std::sync::Arc;
use std::thread;
use std::os::fd::AsRawFd;
use std::ops::Deref;
use std::ptr;
use std::arch::x86_64::{_mm_crc32_u64,_mm_crc32_u8};

unsafe extern "C" { fn mmap(a:*mut u8,n:usize,p:i32,f:i32,fd:i32,o:isize)->*mut u8; fn munmap(a:*mut u8,n:usize)->i32; fn prctl(option:i32,arg2:usize,arg3:usize,arg4:usize,arg5:usize)->i32; }
struct Mapping{address:usize,len:usize}
impl Mapping{fn open(path:&str)->io::Result<Self>{let f=fs::File::open(path)?;let len=f.metadata()?.len()as usize;if len==0{return Ok(Self{address:1,len:0})}let p=unsafe{mmap(ptr::null_mut(),len,1,2,f.as_raw_fd(),0)};if p as isize == -1{return Err(io::Error::last_os_error())}Ok(Self{address:p as usize,len})}}
impl Deref for Mapping{type Target=[u8];fn deref(&self)->&[u8]{unsafe{std::slice::from_raw_parts(self.address as*const u8,self.len)}}}
impl Drop for Mapping{fn drop(&mut self){if self.len!=0{unsafe{munmap(self.address as*mut u8,self.len);}}}}

const HOURS: usize = 72;

#[derive(Clone, Copy)]
struct Record { endpoint: u16, error: bool, value: u32 }
struct Local { endpoints: Vec<Vec<u8>>, records: Vec<Vec<Record>>, remap: Vec<u16> }
struct EndpointTable { slots: [u64;4096], endpoints: Vec<Vec<u8>> }
impl EndpointTable {
    fn new()->Self{Self{slots:[0;4096],endpoints:Vec::with_capacity(1842)}}
    #[inline(always)] fn id(&mut self,key:&[u8])->u16{let mut crc=0u64;let mut p=0;unsafe{while p+8<=key.len(){crc=_mm_crc32_u64(crc,(key.as_ptr().add(p)as*const u64).read_unaligned());p+=8}while p<key.len(){crc=_mm_crc32_u8(crc as u32,key[p])as u64;p+=1}}let h=crc.wrapping_mul(0x9e3779b97f4a7c15)^(key.len()as u64);let mut s=h as usize&4095;loop{let x=self.slots[s];if x==0{let id=self.endpoints.len()as u16;self.endpoints.push(key.to_vec());self.slots[s]=(h&0xffff_ffff_0000_0000)|(id as u64+1);return id}let id=x as u32-1;if x>>32==h>>32&&self.endpoints[id as usize]==key{return id as u16}s=(s+1)&4095}}
}

#[inline(always)]
fn two(p: &[u8], at: usize) -> usize {
    ((p[at] - b'0') * 10 + p[at + 1] - b'0') as usize
}

#[inline(always)]
fn number(p: &[u8]) -> u32 {
    let mut value = 0u32;
    for &c in p {
        value = value * 10 + (c - b'0') as u32;
    }
    value
}

fn parse(data: &[u8], mut begin: usize, mut end: usize) -> Local {
    if begin != 0 {
        while data[begin - 1] != b'\n' { begin += 1; }
    }
    if end != data.len() {
        while data[end - 1] != b'\n' { end += 1; }
    }
    let mut ids = EndpointTable::new();
    let mut records: Vec<Vec<Record>> = (0..HOURS).map(|_| Vec::new()).collect();
    let mut p = begin;
    while p < end {
        let month = two(data, p + 5);
        let day = two(data, p + 8);
        let hour = two(data, p + 11);
        let h = (if month == 10 { day - 30 } else { day + 1 }) * 24 + hour - 18;
        p += 28;
        while data[p] != b' ' { p += 1; }
        p += 1;
        let start = p;
        while data[p] != b' ' { p += 1; }
        let endpoint = &data[start..p];
        let error = data[p + 1] == b'5';
        p += 5;
        let start_value = p;
        while data[p] != b'\n' { p += 1; }
        let value = number(&data[start_value..p]);
        p += 1;
        let id = ids.id(endpoint);
        records[h].push(Record { endpoint: id, error, value });
    }
    Local { endpoints:ids.endpoints, records, remap: Vec::new() }
}

fn main() -> io::Result<()> {
    unsafe { prctl(41,1,0,0,0); }
    let data = Arc::new(Mapping::open("/data/requests.log")?);
    let workers = if data.len() < 1 << 20 { 1 } else { 12 };
    let mut joins = Vec::with_capacity(workers);
    for w in 0..workers {
        let input = data.clone();
        joins.push(thread::spawn(move || {
            let begin = input.len() * w / workers;
            let end = input.len() * (w + 1) / workers;
            parse(&input, begin, end)
        }));
    }
    let mut locals: Vec<_> = joins.into_iter().map(|j| j.join().unwrap()).collect();
    let mut global: HashMap<Vec<u8>,u16> = HashMap::with_capacity(2048);
    let mut endpoints = Vec::with_capacity(1842);
    for local in &mut locals { for endpoint in &local.endpoints { let id=if let Some(&id)=global.get(endpoint){id}else{let id=endpoints.len()as u16;global.insert(endpoint.clone(),id);endpoints.push(endpoint.clone());id};local.remap.push(id); } }
    endpoints.sort_unstable();
    let mut ranks=vec![0u16;endpoints.len()];for(i,e)in endpoints.iter().enumerate(){ranks[global[e]as usize]=i as u16}for local in &mut locals{for id in &mut local.remap{*id=ranks[*id as usize]}}
    let next=std::sync::atomic::AtomicUsize::new(0);let outputs=std::sync::Mutex::new((0..HOURS).map(|_|None).collect::<Vec<Option<Vec<u8>>>>());
    thread::scope(|scope|for _ in 0..workers{scope.spawn(||loop{let h=next.fetch_add(1,std::sync::atomic::Ordering::Relaxed);if h>=HOURS{break}
        let nend=endpoints.len();let mut counts=vec![0u32;nend];let mut errors=vec![0u32;nend];let mut total=0usize;
        for local in &locals{for r in &local.records[h]{let id=local.remap[r.endpoint as usize]as usize;counts[id]+=1;errors[id]+=r.error as u32;total+=1}}
        let mut offsets=vec![0usize;nend+1];for i in 0..nend{offsets[i+1]=offsets[i]+counts[i]as usize}let mut cursor=offsets[..nend].to_vec();let mut values=vec![0u32;total];
        for local in &locals{for r in &local.records[h]{let id=local.remap[r.endpoint as usize]as usize;values[cursor[id]]=r.value;cursor[id]+=1}}
        let absolute=h+18;let day=absolute/24;let month=if day<2{10}else{11};let date=if day<2{day+30}else{day-1};let mut output=Vec::with_capacity(140000);
        for (id,endpoint) in endpoints.iter().enumerate(){let count=counts[id];if count==0{continue}let slice=&mut values[offsets[id]..offsets[id+1]];let n=slice.len();let rank=|percent:usize|(percent*n).div_ceil(100)-1;let k50=rank(50);let k95=rank(95);let k99=rank(99);let(p50,p95,p99);if n<64{slice.sort_unstable();p50=slice[k50];p95=slice[k95];p99=slice[k99]}else{let(lo99,v99,_)=slice.select_nth_unstable(k99);p99=*v99;let(lo95,v95,_)=lo99.select_nth_unstable(k95);p95=*v95;let(_,v50,_)=lo95.select_nth_unstable(k50);p50=*v50}
            write!(output,"2026-{month:02}-{date:02}T{:02}:00:00Z ",absolute%24).unwrap();output.extend_from_slice(endpoint);writeln!(output," {} {} {} {} {} {}",count,p50,p95,p99,errors[id],errors[id]as u64*1_000_000/count as u64).unwrap();}
        outputs.lock().unwrap()[h]=Some(output)});});
    let mut output=Vec::with_capacity(11<<20);for part in outputs.into_inner().unwrap(){output.extend_from_slice(&part.unwrap())}fs::write("/out/summary.txt", output)
}
