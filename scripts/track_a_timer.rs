// Local benchmarking wrapper. Never included in scored track-a/ Docker context.
// Measures process-only launch-to-exit time, without Docker startup, and reads
// cgroup-v2 memory.peak if available. Every output still goes through check.py.
use std::fs;
use std::process::{self, Command};
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
