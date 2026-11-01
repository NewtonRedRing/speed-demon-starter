// Local benchmarking wrapper. Never included in scored track-a/ Docker context.
// Measures process-only launch-to-exit time, without Docker startup, and reads
// cgroup-v2 memory.peak if available. Every output still goes through check.py.
use std::ffi::c_long;
use std::fs;
use std::process::{self, Command};

// Linux x86_64: struct rusage = two timeval pairs followed by 14 longs.
// RUSAGE_CHILDREN counts only the one scored child, excluding the timer itself.
#[repr(C)]
struct RUsage {
    cpu_times: [TimeVal; 2],
    counts: [c_long; 14],
}
#[repr(C)]
struct TimeVal {
    sec: c_long,
    usec: c_long,
}
extern "C" {
    fn getrusage(who: i32, usage: *mut RUsage) -> i32;
}
use std::time::Instant;

fn main() {
    let start = Instant::now();
    let status = match Command::new("/app/solution").status() {
        Ok(status) => status,
        Err(err) => {
            eprintln!("benchmark timer failed to start Track A: {err}");
            process::exit(126);
        }
    };
    let ns = start.elapsed().as_nanos();
    eprintln!("SPEED_DEMON_PROCESS_NS={ns}");
    unsafe {
        let mut usage: RUsage = std::mem::zeroed();
        if getrusage(-1, &mut usage) == 0 { // RUSAGE_CHILDREN
            eprintln!("SPEED_DEMON_MINOR_FAULTS={}", usage.counts[4]);
            eprintln!("SPEED_DEMON_MAJOR_FAULTS={}", usage.counts[5]);
        }
    }
    match fs::read_to_string("/sys/fs/cgroup/memory.peak") {
        Ok(peak) => {
            if let Ok(bytes) = peak.trim().parse::<u64>() {
                eprintln!("SPEED_DEMON_CGROUP_PEAK_BYTES={bytes}");
            }
        }
        Err(_) => {
            eprintln!("NOTE: cgroup memory.peak unavailable in this Docker VM");
        }
    }
    match status.code() {
        Some(code) => process::exit(code),
        None => process::exit(128),
    }
}
