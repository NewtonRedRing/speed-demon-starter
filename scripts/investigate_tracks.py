#!/usr/bin/env python3
"""Full-public-input, paired experiments for A-D; never edits a submission.

Production offline Dockerfiles (including their PGO) are used unchanged.
Local child timing and cgroup peaks are not official scoring-machine results.
"""
import argparse
import json
from pathlib import Path
import random
import shutil
import statistics
from tempfile import TemporaryDirectory
import uuid

import investigate_track_a as probe

ROOT = probe.ROOT
OPTIONS = {
    "a": ("release-scratch", "trim-heap", "release-trim", "summary-mmaps", "release-mmaps"),
    "b": ("shard-posts", "density-16", "skip-unused", "shard-density-16"),
    "c": ("long-name-views", "batch-32"),
    "d": ("six-byte-records", "blob-256", "blob-1024"),
}
TARGETS = {"a": (16, 18_000_000), "b": (106, 41_000_000),
           "c": (20, 11_000_000), "d": (120, 66_000_000)}


def variant(track, name, source):
    change = probe.replace
    if name == "baseline":
        return source
    if name == "release-trim":
        return variant("a", "trim-heap", variant("a", "release-scratch", source))
    if name == "release-mmaps":
        return variant("a", "summary-mmaps", variant("a", "release-scratch", source))
    if name == "summary-mmaps":
        source = change(source, 'extern "C" {', 'extern "C" {\n    fn mallopt(param: i32, value: i32) -> i32;')
        return change(source, "fn main() {",
                      'fn main() {\n    assert_eq!(unsafe { mallopt(-3, 32 << 10) }, 1, "mallopt");')
    if name == "trim-heap":
        source = change(source, 'extern "C" {', 'extern "C" {\n    fn malloc_trim(pad: usize) -> i32;')
        return change(source, "if barrier.wait().is_leader() {\n            let mut total",
                      "if barrier.wait().is_leader() {\n            malloc_trim(0);\n            let mut total")
    if track == "a":
        return probe.variant(source, name)
    if name == "shard-density-16":
        return variant("b", "density-16", variant("b", "shard-posts", source))
    if name == "shard-posts":
        begin = source.index("    // Resolve each posting's shard-local")
        end = source.index("    let bits: Vec<u64>", begin)
        source = source[:begin] + source[end:]
        return change(source,
                      "i => Docs::List(Cow::Borrowed(&posting_docs[start[i] as usize..posting_end[i]])),",
                      """i => {
            let mut at = start[i] as usize;
            let segs = &seg[i * THREADS..][..THREADS];
            let mut docs = Vec::with_capacity(segs.iter().map(|&n| n as usize).sum());
            for (&n, &first) in segs.iter().zip(&firsts) {
                docs.extend(post[at..at + n as usize].iter().map(|&d| first + d as u32));
                at += n as usize;
            }
            Docs::List(Cow::Owned(docs))
        }""")
    if name == "density-16":
        return change(source, "const BITMAP_DENSITY_DIVISOR: usize = 32;",
                      "const BITMAP_DENSITY_DIVISOR: usize = 16;")
    if name == "skip-unused":
        return change(source, "let i = (id as usize).min(nkeep);",
                      "if id as usize >= nkeep { return; }\n            let i = id as usize;")
    if name == "batch-32":
        return change(source, "const BATCH: usize = 16;", "const BATCH: usize = 32;")
    if name == "long-name-views":
        source = change(source, "const NAME_ARENA: usize = 1 << 20;", "")
        begin = source.index("struct MetaCell(")
        end = source.index("\nstruct Names {", begin)
        source = source[:begin] + """// Input views are read only while scan's mapped input and padded tail are alive.
struct LongName(UnsafeCell<std::mem::MaybeUninit<(usize, usize)>>);
unsafe impl Sync for LongName {}
""" + source[end:]
        source = change(source, """    meta: Vec<MetaCell>, // long only: raw_len << 32 | tail_offset
    arena: ByteArena,""", "    long: Vec<LongName>,")
        source = change(source, """            meta: (0..NAME_SLOTS).map(|_| MetaCell(UnsafeCell::new(0))).collect(),
            arena: ByteArena(UnsafeCell::new(vec![0u8; NAME_ARENA])),""",
                        """            long: (0..NAME_SLOTS).map(|_| LongName(UnsafeCell::new(std::mem::MaybeUninit::uninit()))).collect(),""")
        begin = source.index("    #[inline(always)]\n    fn long_same(")
        end = source.index("    fn distinct(", begin)
        source = source[:begin] + """    #[inline(always)]
    fn long_same(&self, id: usize, p: usize, len: usize) -> bool {
        let (stored, n) = unsafe { (*self.long[id].0.get()).assume_init() };
        n == len && unsafe {
            std::slice::from_raw_parts(stored as *const u8, n)
                == std::slice::from_raw_parts(p as *const u8, len)
        }
    }

    #[inline(always)]
    fn number(&self, h: u64, p: usize, len: usize) -> usize {
        let (raw_lo, raw_hi) = unsafe { name_words(p, len) };
        let tag = LONG | ((h & 0x7fff_ffff) << 32);
        let ready_lo = raw_lo | OCCUPIED;
        let mut i = h as usize & (NAME_SLOTS - 1);
        loop {
            let mut lo = self.slots[i].lo.load(Acquire);
            while lo == BUSY {
                std::hint::spin_loop();
                lo = self.slots[i].lo.load(Acquire);
            }
            if lo == 0 {
                if self.slots[i].lo.compare_exchange(0, BUSY, Acquire, Acquire).is_ok() {
                    let hi = if len > 16 {
                        let id = self.used.fetch_add(1, Relaxed) as usize;
                        assert!(id < self.long.len(), "long name table exhausted");
                        unsafe { *self.long[id].0.get() = std::mem::MaybeUninit::new((p, len)); }
                        tag | id as u64
                    } else { raw_hi };
                    unsafe { *self.slots[i].hi.get() = hi; }
                    self.slots[i].lo.store(ready_lo, Release);
                    return i;
                }
                continue;
            }
            if (lo & !OCCUPIED) == raw_lo {
                let hi = unsafe { *self.slots[i].hi.get() };
                if (len <= 16 && hi == raw_hi)
                    || (len > 16 && hi & !0xffff_ffff == tag && self.long_same(hi as u32 as usize, p, len))
                {
                    return i;
                }
            }
            i = (i + 1) & (NAME_SLOTS - 1);
        }
    }

""" + source[end:]
        return source
    if name in ("blob-256", "blob-1024"):
        size = name.split("-")[1]
        return change(source, "const BLOB: usize = 512;", f"const BLOB: usize = {size};")
    if name == "six-byte-records":
        source = change(source, "fn main() {", """fn unpack_record(r: [u8; 6]) -> u64 {
    u32::from_le_bytes(r[..4].try_into().unwrap()) as u64
        | (u16::from_le_bytes(r[4..].try_into().unwrap()) as u64) << 32
}

fn main() {""")
        source = change(source, "type Bucket = (Vec<u64>, Vec<u32>);",
                        "type Bucket = (Vec<[u8; 6]>, Vec<u32>);")
        source = change(source,
                        "b.0.push(((u & ((1 << sh) - 1)) as u64) << 32 | (v | w << hb) as u64);",
                        """let r = (((u & ((1 << sh) - 1)) as u64) << 32 | (v | w << hb) as u64).to_le_bytes();
            b.0.push(r[..6].try_into().unwrap());""")
        source = change(source, "cursor[(r >> 32) as usize] += 1",
                        "cursor[(unpack_record(r) >> 32) as usize] += 1")
        return change(source, "for &r in records {\n                    let c",
                      "for &r in records {\n                    let r = unpack_record(r);\n                    let c")
    raise ValueError((track, name))


def build(track, name, directory, prefix, base_ref):
    context = directory / f"{track}-{name}"
    context.mkdir()
    for filename in probe.run(["git", "ls-tree", "--name-only", f"{base_ref}:track-{track}"]).splitlines():
        (context / filename).write_bytes(probe.source_at(base_ref, track, filename))
    main = context / "main.rs"
    main.write_text(variant(track, name, main.read_text(encoding="utf-8")), encoding="utf-8")
    lines = probe.count_source(context, main.read_text(encoding="utf-8"), (context / "Dockerfile").read_bytes())
    if track == "c":
        checked = context / "checked-main.rs"
        checked.write_text("\n".join(
            line.replace("//!", "//", 1) if line.startswith("//!") else line
            for line in main.read_text(encoding="utf-8").splitlines()
        ), encoding="utf-8")
        checker = probe.run(["docker", "create", "--network=none", "--entrypoint", "sh",
            probe.BUILDER, "-c",
            "rustc --edition=2021 --test -C target-cpu=native -C opt-level=2 /tmp/checks.rs -o /tmp/checks && /tmp/checks"])
        try:
            probe.run(["docker", "cp", checked, f"{checker}:/tmp/checked-main.rs"])
            probe.run(["docker", "cp", ROOT / "tests" / "track_c_probe_checks.rs", f"{checker}:/tmp/checks.rs"])
            print(probe.run(["docker", "start", "--attach", checker]), flush=True)
        finally:
            probe.run(["docker", "rm", checker])
    tag = f"{prefix}-{track}-{name}"
    probe.run(["docker", "build", "--network=none", "-t", tag, context])
    return tag, lines


def timed_image(tag, directory):
    context = directory / "timer"
    context.mkdir(exist_ok=True)
    shutil.copyfile(ROOT / "scripts" / "track_a_probe.rs", context / "probe.rs")
    (context / "Dockerfile").write_text(f"""FROM {probe.BUILDER} AS build
COPY probe.rs /app/probe.rs
RUN rustc --edition=2021 -C opt-level=2 -C panic=abort -C strip=symbols -o /app/probe /app/probe.rs
FROM {tag}
COPY --from=build /app/probe /app/probe
ENTRYPOINT ["/app/probe"]
""", encoding="utf-8")
    timed = tag + "-timed"
    probe.run(["docker", "build", "--network=none", "-t", timed, context])
    return timed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tracks", nargs="+", choices=list(OPTIONS), default=list(OPTIONS))
    parser.add_argument("--variants", nargs="+", default=["baseline"])
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--base-ref", default=probe.BASELINE_REF)
    parser.add_argument("--out-fs", choices=["tmpfs", "disk"], default="tmpfs")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.runs < 3 or "baseline" not in args.variants:
        parser.error("Use at least three repetitions and include baseline")
    unknown = set(args.variants) - {"baseline"} - {n for names in OPTIONS.values() for n in names}
    if unknown:
        parser.error(f"Unknown variants: {sorted(unknown)}")
    unused = set(args.variants) - {"baseline"} - {n for t in args.tracks for n in OPTIONS[t]}
    if unused:
        parser.error(f"Variants do not apply to the selected tracks: {sorted(unused)}")
    if len(set(args.variants)) != len(args.variants) or len(set(args.tracks)) != len(args.tracks):
        parser.error("Specify each track and variant only once")
    if any(args.output.resolve().is_relative_to((ROOT / f"track-{t}").resolve()) for t in OPTIONS):
        parser.error("Keep reports outside submission directories")
    (ROOT / ".out").mkdir(exist_ok=True)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    prefix = f"sd-probe-all-{uuid.uuid4().hex[:12]}"
    report = {"commit": probe.run(["git", "rev-parse", "HEAD"]), "base_ref": args.base_ref,
              "out_fs": args.out_fs, "fresh_output": True, "tracks": {}}
    images, containers, volumes = [], [], []

    def save():
        args.output.write_text(json.dumps(report, indent=2), encoding="utf-8")

    try:
        with TemporaryDirectory(prefix=prefix, dir=ROOT / ".out") as temp:
            directory = Path(temp)
            for track in args.tracks:
                data = probe.official_data.verify(track)
                names = [n for n in args.variants if n == "baseline" or n in OPTIONS[track]]
                results = report["tracks"][track] = {"target_ms": TARGETS[track][0],
                    "target_bytes": TARGETS[track][1], "variants": {n: [] for n in names}}
                tags = {}
                code_lines = {}
                for name in names:
                    tag, lines = build(track, name, directory, prefix, args.base_ref)
                    code_lines[name] = lines
                    images.append(tag)
                    timed = timed_image(tag, directory)
                    images.append(timed)
                    tags[name] = timed
                data_volume, out_volume = f"{prefix}-{track}-data", f"{prefix}-{track}-out"
                for volume in (data_volume, out_volume):
                    options = (["--driver", "local", "--opt", "type=tmpfs",
                                "--opt", "device=tmpfs", "--opt", "o=size=1g,mode=0777"]
                               if volume == out_volume and args.out_fs == "tmpfs" else [])
                    probe.run(["docker", "volume", "create", *options, volume])
                    volumes.append(volume)
                stager = probe.run(["docker", "create", "--network=none", "--entrypoint", "sh",
                    "-v", f"{data_volume}:/data", "-v", f"{out_volume}:/out",
                    probe.RUNTIME, "-c", "sleep 3600"])
                containers.append(stager)
                probe.run(["docker", "start", stager])
                for filename, expected in probe.official_data.TRACKS[track]["inputs"].items():
                    probe.run(["docker", "cp", data / filename, f"{stager}:/data/{filename}"])
                    digest = probe.run(["docker", "exec", stager, "sha256sum", f"/data/{filename}"]).split()[0]
                    if digest != expected:
                        raise RuntimeError(f"Staged input hash mismatch: {track}/{filename}")
                probe.run(["docker", "exec", stager, "sh", "-c",
                           "chmod 755 /data; chmod 644 /data/*; chmod 777 /out"])
                for repetition in range(args.runs + 1):
                    candidates = [n for n in names if n != "baseline"]
                    random.Random(repetition).shuffle(candidates)
                    order = [item for i, n in enumerate(candidates) for item in (
                        ("baseline", n) if (repetition + i) % 2 == 0 else (n, "baseline")
                    )] if candidates else ["baseline"]
                    previous = None
                    for index, name in enumerate(order):
                        probe.run(["docker", "exec", stager, "sh", "-c", "cat /data/* >/dev/null"])
                        record = probe.measure(tags[name], data_volume, out_volume, track=track)
                        if repetition:
                            results["variants"][name].append(record)
                            if candidates and index % 2:
                                control, candidate = ((record, previous) if name == "baseline" else (previous, record))
                                candidate["control"] = control.copy()
                        previous = record
                        print(f"{track.upper()} {repetition:02} {name:18} {record['ns']/1e6:8.3f} ms "
                              f"{record['peak_bytes']/1e6:7.3f} MB", flush=True)
                        save()
                probe.official_data.verify(track)
                for name, records in results["variants"].items():
                    summary = {"median_ms": statistics.median(r["ns"] for r in records) / 1e6,
                               "maximum_peak_bytes": max(r["peak_bytes"] for r in records),
                               "code_lines": code_lines[name]}
                    walls = sorted(r["ns"] for r in records)
                    summary["trimmed_ms"] = statistics.mean(walls[1:-1]) / 1e6
                    summary["local_targets_met"] = (summary["trimmed_ms"] <= TARGETS[track][0]
                        and summary["maximum_peak_bytes"] <= TARGETS[track][1])
                    if name != "baseline":
                        ratios = [r["ns"] / r["control"]["ns"] for r in records]
                        summary["paired_time_ratio"] = statistics.median(ratios)
                        summary["paired_time_ratio_bootstrap_95"] = probe.median_interval(ratios)
                    results[name] = summary
                    print(track, name, summary, flush=True)
                save()
    finally:
        probe.check.cleanup_docker(containers, volumes)
        if images:
            probe.run(["docker", "image", "rm", *reversed(images)])


if __name__ == "__main__":
    main()
