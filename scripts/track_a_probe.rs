// Local Linux/x86-64 probe; never copied into the scored submission.
use std::{ffi::c_void, fs, os::fd::AsRawFd, process::Command, time::Instant};

#[repr(C)]
#[derive(Default)]
struct Usage {
    user: [i64; 2],
    system: [i64; 2],
    rss: i64,
    shared: i64,
    data: i64,
    stack: i64,
    minor: i64,
    major: i64,
    rest: [i64; 8],
}

extern "C" {
    fn getrusage(who: i32, usage: *mut Usage) -> i32;
    fn syscall(number: i64, ...) -> i64;
    fn close(fd: i32) -> i32;
    fn mmap(addr: *mut c_void, len: usize, prot: i32, flags: i32, fd: i32, off: i64) -> *mut u8;
    fn madvise(addr: *mut c_void, len: usize, advice: i32) -> i32;
}

fn mapping_probe() {
    let file = fs::File::open("/data/requests.log").expect("input");
    let size = file.metadata().expect("fstat").len() as usize;
    let p = unsafe { mmap(std::ptr::null_mut(), size, 1, 2, file.as_raw_fd(), 0) };
    assert_ne!(p as isize, -1, "mmap");
    let mut before = Usage::default();
    assert_eq!(unsafe { getrusage(0, &mut before) }, 0);
    let start = Instant::now();
    let mode = std::env::args().nth(2).expect("mapping mode");
    let advice = match mode.as_str() {
        "baseline" => None,
        "populate" => Some(22),
        "hugepage" => Some(14),
        _ => panic!("unknown mapping mode: {mode}"),
    };
    if let Some(advice) = advice {
        assert_eq!(unsafe { madvise(p.cast(), size, advice) }, 0, "madvise");
    }
    for offset in (0..size).step_by(4096) {
        unsafe { p.add(offset).read_volatile() };
    }
    let elapsed = start.elapsed().as_nanos();
    let mut after = Usage::default();
    assert_eq!(unsafe { getrusage(0, &mut after) }, 0);
    println!(
        "{{\"mode\":\"{mode}\",\"ns\":{elapsed},\"minor_faults\":{},\"major_faults\":{}}}",
        after.minor - before.minor,
        after.major - before.major
    );
    let smaps = fs::read_to_string("/proc/self/smaps").expect("smaps");
    let mut input = false;
    for line in smaps.lines() {
        if line.as_bytes()[0].is_ascii_hexdigit() && line.contains('-') {
            input = line.ends_with("/data/requests.log");
        }
        if input
            && (line.contains("requests.log")
                || [
                    "KernelPageSize:",
                    "MMUPageSize:",
                    "FilePmdMapped:",
                    "THPeligible:",
                    "VmFlags:",
                ]
                .iter()
                .any(|key| line.starts_with(key)))
        {
            println!("{line}");
        }
    }
}

fn main() {
    if std::env::args().nth(1).as_deref() == Some("mapping") {
        mapping_probe();
        return;
    }
    if std::env::args().nth(1).as_deref() == Some("environment") {
        let mut attr = [0u64; 16];
        attr[0] = 1 | (128 << 32); // PERF_TYPE_SOFTWARE, perf_event_attr size
        attr[1] = 5; // PERF_COUNT_SW_PAGE_FAULTS_MIN
        attr[5] = 1 | (1 << 5) | (1 << 6); // disabled, exclude kernel/hypervisor
        let fd = unsafe { syscall(298, attr.as_ptr(), 0i32, -1i32, -1i32, 0u64) };
        if fd < 0 {
            println!("perf_event_open: {}", std::io::Error::last_os_error());
        } else {
            println!("perf_event_open: permitted");
            unsafe { close(fd as i32) };
        }
        return;
    }
    let start = Instant::now();
    let status = Command::new("/app/solution")
        .status()
        .expect("launch solution");
    let elapsed = start.elapsed().as_nanos();
    assert!(status.success(), "solution failed: {status}");
    let mut usage = Usage::default();
    assert_eq!(unsafe { getrusage(-1, &mut usage) }, 0, "getrusage");
    let peak = fs::read_to_string("/sys/fs/cgroup/memory.peak")
        .expect("cgroup v2 memory.peak")
        .trim()
        .parse::<u64>()
        .expect("numeric peak");
    println!(
        "{{\"ns\":{elapsed},\"peak_bytes\":{peak},\"rss_kib\":{},\"minor_faults\":{},\"major_faults\":{}}}",
        usage.rss, usage.minor, usage.major
    );
}
