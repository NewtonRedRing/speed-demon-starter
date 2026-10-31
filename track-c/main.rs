use std::collections::HashMap;
use std::fs;
use std::io::{self, Write};
use std::sync::{Arc, Mutex};
use std::thread;
use std::arch::x86_64::{_mm_crc32_u64,_mm_crc32_u8};
use std::os::fd::AsRawFd;use std::ops::Deref;use std::ptr;
unsafe extern "C"{fn mmap(a:*mut u8,n:usize,p:i32,f:i32,fd:i32,o:isize)->*mut u8;fn munmap(a:*mut u8,n:usize)->i32;}
struct Mapping{address:usize,len:usize}impl Mapping{fn open(path:&str)->io::Result<Self>{let f=fs::File::open(path)?;let len=f.metadata()?.len()as usize;let p=unsafe{mmap(ptr::null_mut(),len,1,2,f.as_raw_fd(),0)};if p as isize == -1{return Err(io::Error::last_os_error())}Ok(Self{address:p as usize,len})}}impl Deref for Mapping{type Target=[u8];fn deref(&self)->&[u8]{unsafe{std::slice::from_raw_parts(self.address as*const u8,self.len)}}}impl Drop for Mapping{fn drop(&mut self){unsafe{munmap(self.address as*mut u8,self.len);}}}

#[derive(Clone, Copy)]
struct Aggregate { count:u64,id_sum:u64,sum:i64,min:i64,max:i64,distinct:u64,name_bytes:u64,note_bytes:u64 }
impl Default for Aggregate { fn default()->Self{Self{count:0,id_sum:0,sum:0,min:i64::MAX,max:i64::MIN,distinct:0,name_bytes:0,note_bytes:0}} }

#[inline(always)] fn number(data:&[u8],p:&mut usize)->u64{let mut v=0;while *p<data.len()&&data[*p].is_ascii_digit(){v=v*10+(data[*p]-b'0')as u64;*p+=1}v}
#[inline(always)] fn skip_bom(data:&[u8],p:&mut usize){if data.get(*p..*p+3)==Some(&[0xef,0xbb,0xbf]){*p+=3}}

fn field(data:&[u8],p:&mut usize,decode:bool)->(Vec<u8>,usize){
    if data[*p]!=b'"'{let s=*p;while data[*p]!=b','{*p+=1}let out=if decode{data[s..*p].to_vec()}else{Vec::new()};let n=*p-s;*p+=1;return(out,n)}
    *p+=1;let mut out=Vec::new();let mut n=0;
    loop{let c=data[*p];*p+=1;if c==b'"'{if data[*p]==b'"'{if decode{out.push(b'"')}n+=1;*p+=1}else{break}}else{if decode{out.push(c)}n+=1}}
    assert_eq!(data[*p],b',');*p+=1;(out,n)
}

#[inline(always)]fn hash(bytes:&[u8])->u64{let mut h=bytes.len()as u64;let mut p=0;unsafe{while p+8<=bytes.len(){h=_mm_crc32_u64(h,(bytes.as_ptr().add(p)as*const u64).read_unaligned());p+=8}while p<bytes.len(){h=_mm_crc32_u8(h as u32,bytes[p])as u64;p+=1}}h.wrapping_mul(0x9e3779b97f4a7c15)}

fn main()->io::Result<()>{
    let regions_data=fs::read("/data/regions.csv")?;let mut p=b"region_id,country\n".len();let mut country_ids:HashMap<[u8;3],u8>=HashMap::new();let mut countries=vec!["_UNMATCHED".to_string()];let mut regions=Vec::<u8>::new();
    while p<regions_data.len(){let r=number(&regions_data,&mut p)as usize;p+=1;let code:[u8;3]=regions_data[p..p+3].try_into().unwrap();p+=4;let id=*country_ids.entry(code).or_insert_with(||{countries.push(String::from_utf8(code.to_vec()).unwrap());(countries.len()-1)as u8});if r>=regions.len(){regions.resize(r+1,0)}regions[r]=id}
    let data=Arc::new(Mapping::open("/data/records.csv")?);p=0;skip_bom(&data,&mut p);let header=b"id,name,note,region_id,amount";assert_eq!(&data[p..p+header.len()],header);p+=header.len();if data[p]==b'\r'{p+=1}p+=1;
    let workers=if data.len()<1<<20{1}else{12};let parity:Vec<_>=(0..workers).map(|_|std::sync::atomic::AtomicBool::new(false)).collect();thread::scope(|scope|for w in 0..workers-1{let input=&data;let slot=&parity[w];scope.spawn(move||{let a=input.len()*w/workers;let b=input.len()*(w+1)/workers;slot.store(input[a..b].iter().filter(|&&c|c==b'"').count()&1==1,std::sync::atomic::Ordering::Relaxed);});});
    let mut bounds=vec![0usize;workers+1];bounds[0]=p;bounds[workers]=data.len();let mut quoted=false;for w in 1..workers{quoted^=parity[w-1].load(std::sync::atomic::Ordering::Relaxed);let mut q=data.len()*w/workers;let mut inside=quoted;while q<data.len(){let c=data[q];q+=1;if c==b'"'{inside=!inside}if c==b'\n'&&!inside{break}}bounds[w]=q}
    let stripes:Arc<Vec<Mutex<HashMap<Vec<u8>,[u64;4]>>>>=Arc::new((0..1024).map(|_|Mutex::new(HashMap::new())).collect());let regions=Arc::new(regions);let mut joins=Vec::new();
    for w in 0..workers{let input=data.clone();let table=stripes.clone();let rmap=regions.clone();let begin=bounds[w];let end=bounds[w+1];let count=countries.len();joins.push(thread::spawn(move||{let mut a=vec![Aggregate::default();count];let mut p=begin;while p<end{skip_bom(&input,&mut p);let id=number(&input,&mut p);p+=1;let(name,nb)=field(&input,&mut p,true);let(_,tb)=field(&input,&mut p,false);let region=number(&input,&mut p)as usize;p+=1;let neg=input[p]==b'-';if neg{p+=1}let whole=number(&input,&mut p)as i64;p+=1;let cents=whole*100+((input[p]-b'0')as i64)*10+(input[p+1]-b'0')as i64;p+=2;if p<input.len()&&input[p]==b'\r'{p+=1}if p<input.len(){p+=1}let country=if region<rmap.len(){rmap[region]as usize}else{0};let value=if neg{-cents}else{cents};let x=&mut a[country];x.count+=1;x.id_sum+=id;x.sum+=value;x.min=x.min.min(value);x.max=x.max.max(value);x.name_bytes+=nb as u64;x.note_bytes+=tb as u64;let h=hash(&name);let mut stripe=table[h as usize&1023].lock().unwrap();let bits=stripe.entry(name).or_insert([0;4]);let word=country/64;let bit=1u64<<(country%64);if bits[word]&bit==0{bits[word]|=bit;x.distinct+=1}}
        a}));}
    let mut total=vec![Aggregate::default();countries.len()];for j in joins{let local=j.join().unwrap();for(i,s)in local.into_iter().enumerate(){let t=&mut total[i];t.count+=s.count;t.id_sum+=s.id_sum;t.sum+=s.sum;t.min=t.min.min(s.min);t.max=t.max.max(s.max);t.distinct+=s.distinct;t.name_bytes+=s.name_bytes;t.note_bytes+=s.note_bytes}}
    let mut order:Vec<_>=(0..countries.len()).collect();order.sort_unstable_by_key(|&i|&countries[i]);let money=|v:i64|{let n=v.unsigned_abs();format!("{}{}.{:02}",if v<0{"-"}else{""},n/100,n%100)};let mut out=Vec::new();for i in order{let a=total[i];if a.count==0{continue}writeln!(out,"{} {} {} {} {} {} {} {} {}",countries[i],a.count,a.id_sum,money(a.sum),money(a.min),money(a.max),a.distinct,a.name_bytes,a.note_bytes)?}fs::write("/out/aggregates.txt",out)
}
