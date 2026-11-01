#!/usr/bin/env python3
"""Isolated Track A experiments; never changes the production submission.

Offline builds, original full seed-1 SHA gates, interleaved process timings,
getrusage fault counts, cgroup peaks, and rustfmt/scc code-line counts.
Local results are NOT board scores. Requires Linux Docker and cached images.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import random
import statistics
import subprocess
import sys
from tempfile import TemporaryDirectory
import uuid

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import check
import official_data

BUILDER = "rust:1-slim-trixie@sha256:1a939748a7cbab172de91a014e3804a97af1394a8c844faa67fce9401c4e40fb"
RUNTIME = "debian:13-slim@sha256:62bd423aeee912639ca8ee681c2b762cb3e25137e0a92b20be455898a9f034ad"
TOOLS = "speeddemon/static-tools:2026-10-28"
BASELINE_REF = "7a9886d0635e3deb74493f2c23cde76c30c521c2"
VARIANTS = (
    "baseline", "populate-read", "map-populate", "chunk-no-thp", "input-thp",
    "one-load", "hot-cache", "bucket-interner", "stack-label", "const-label",
    "fallocate", "six-workers", "release-scratch", "strip", "crt-static", "fstat",
    "parse-only", "parse-counts", "final-release",
)


def run(args, *, capture=True):
    result = subprocess.run([str(x) for x in args], capture_output=capture, text=True)
    if result.returncode:
        raise RuntimeError(f"{args}\n{result.stdout or ''}\n{result.stderr or ''}")
    return result.stdout.strip() if capture else ""


def replace(source, old, new):
    if source.count(old) != 1:
        raise RuntimeError(f"Expected exactly one source anchor: {old!r}")
    return source.replace(old, new)


def source_at(ref, track, name):
    return subprocess.check_output(
        ["git", "-C", str(ROOT), "show", f"{ref}:track-{track}/{name}"]
    )


def median_interval(values):
    rng = random.Random(0)
    samples = sorted(statistics.median(rng.choices(values, k=len(values))) for _ in range(5000))
    return [samples[125], samples[4874]]


def variant(source, name):
    if name == "populate-read":
        source = replace(source, "from_raw_parts(p, size)",
                         'assert_eq!(madvise(p.cast(), size, 22), 0, "MADV_POPULATE_READ");\n'
                         "        from_raw_parts(p, size)")
    elif name == "map-populate":
        source = replace(source, "1, 0x12, input, 0", "1, 0x8012, input, 0")
    elif name in ("chunk-no-thp", "input-thp"):
        source = replace(source, "unsafe { prctl(41, 1, 0, 0, 0) };",
                         "// THP is disabled only on the chunk regions.")
        source = replace(source, "let next = unsafe { map(bytes",
                         "let next: *mut Chunk = unsafe { map(bytes")
        source = replace(source, "let wide = vec![vec![]; HOURS];",
                         'assert_eq!(unsafe { madvise(next.cast(), bytes, 15) }, 0, "MADV_NOHUGEPAGE");\n'
                         "        let wide = vec![vec![]; HOURS];")
        if name == "input-thp":
            source = replace(source, "from_raw_parts(p, size)",
                             'assert_eq!(madvise(p.cast(), size, 14), 0, "MADV_HUGEPAGE");\n'
                             "        from_raw_parts(p, size)")
    elif name == "one-load":
        source = replace(source, """let sp = eq32(m, b' ') as u64 | (eq32(m.add(32), b' ') as u64) << 32;
            let nl = eq32(m, b'\\n') as u64 | (eq32(m.add(32), b'\\n') as u64) << 32;""",
                         """let a = _mm256_loadu_si256(m.cast());
            let b = _mm256_loadu_si256(m.add(32).cast());
            let mask = |v, c| _mm256_movemask_epi8(_mm256_cmpeq_epi8(v, _mm256_set1_epi8(c))) as u32 as u64;
            let sp = mask(a, b' ' as i8) | mask(b, b' ' as i8) << 32;
            let nl = mask(a, b'\\n' as i8) | mask(b, b'\\n' as i8) << 32;""")
    elif name == "hot-cache":
        source = replace(source, "slot: Vec<u16>,", "hot: [(u32, u16); 64],\n    slot: Vec<u16>,")
        source = replace(source, "Interner { names: vec![], slot:",
                         "Interner { hot: [(0, 0); 64], names: vec![], slot:")
        source = replace(source, "loop {\n            let id = &mut self.slot",
                         """let hash = i as u32;
        let (tag, id) = self.hot[i % 64];
        if id != 0 && tag == hash && same(self.names[id as usize - 1], s) {
            return id as u32 - 1;
        }
        loop {
            let id = &mut self.slot""")
        source = replace(source, "return *id as u32 - 1;",
                         "self.hot[hash as usize % 64] = (hash, *id);\n                return *id as u32 - 1;")
    elif name == "bucket-interner":
        source = replace(source, "struct Interner {",
                         "#[repr(C, align(64))]\n#[derive(Clone)]\nstruct Bucket([u32; 8]);\n\nstruct Interner {")
        source = replace(source, "slot: Vec<u16>", "slot: Vec<Bucket>")
        source = replace(source, "slot: vec![0; Self::SLOTS]",
                         "slot: vec![Bucket([0; 8]); Self::SLOTS / 8]")
        begin = source.index("        let mut i = unsafe { _mm_crc32_u64")
        end = source.index("\n    }\n}", begin)
        source = source[:begin] + """        let hash = unsafe { _mm_crc32_u64(_mm_crc32_u64(n as u64, a), b) } as u32;
        let tag = hash & 0xffff0000;
        let mut i = hash as usize;
        loop {
            let bucket = &mut self.slot[i % (Self::SLOTS / 8)].0;
            let (mut hits, empty) = unsafe {
                let v = _mm256_loadu_si256(bucket.as_ptr().cast());
                let tags = _mm256_and_si256(v, _mm256_set1_epi32(-65536));
                let mask = |v| _mm256_movemask_ps(_mm256_castsi256_ps(v)) as u32;
                (mask(_mm256_cmpeq_epi32(tags, _mm256_set1_epi32(tag as i32))),
                 mask(_mm256_cmpeq_epi32(v, _mm256_setzero_si256())))
            };
            hits &= !empty;
            while hits != 0 {
                let id = (bucket[hits.trailing_zeros() as usize] & 65535) as usize - 1;
                if same(self.names[id], s) {
                    return id as u32;
                }
                hits &= hits - 1;
            }
            if empty != 0 {
                assert!(self.names.len() < Self::SLOTS / 2, "too many endpoints");
                self.names.push(s);
                bucket[empty.trailing_zeros() as usize] = tag | self.names.len() as u32;
                return self.names.len() as u32 - 1;
            }
            i += 1;
        }""" + source[end:]
    elif name in ("stack-label", "const-label"):
        old = """let date = ["10-30", "10-31", "11-01", "11-02"][(h + 18) / 24];
            let label = format!("2026-{date}T{:02}:00:00Z ", (h + 18) % 24);"""
        make_label = """let date = [*b"10-30", *b"10-31", *b"11-01", *b"11-02"][(h + 18) / 24];
            let mut label = *b"2026-10-30T00:00:00Z ";
            label[5..10].copy_from_slice(&date);
            label[11] = b'0' + ((h + 18) % 24 / 10) as u8;
            label[12] = b'0' + ((h + 18) % 10) as u8;"""
        # The units digit is modulo 10 AFTER wrapping the hour at 24.
        make_label = make_label.replace("((h + 18) % 10)", "((h + 18) % 24 % 10)")
        if name == "stack-label":
            source = replace(source, old, make_label)
        else:
            source = replace(source, "fn main() {", """const LABELS: [[u8; 21]; HOURS] = {
    let mut labels = [[0; 21]; HOURS];
    let mut h = 0;
    while h < HOURS {
        let date = [*b"10-30", *b"10-31", *b"11-01", *b"11-02"][(h + 18) / 24];
        let mut label = *b"2026-10-30T00:00:00Z ";
        let mut i = 0;
        while i < 5 {
            label[5 + i] = date[i];
            i += 1;
        }
        label[11] = b'0' + ((h + 18) % 24 / 10) as u8;
        label[12] = b'0' + ((h + 18) % 24 % 10) as u8;
        labels[h] = label;
        h += 1;
    }
    labels
};

fn main() {""")
            source = replace(source, old, "let label = LABELS[h];")
        source = replace(source, "label.as_bytes()", "&label[..]")
    elif name == "fallocate":
        source = replace(source, 'extern "C" {', 'extern "C" {\n    fn posix_fallocate(fd: i32, offset: i64, len: i64) -> i32;')
        source = replace(source, '*text.get() = map(total as usize',
                         'assert_eq!(posix_fallocate(out, 0, total as i64), 0, "posix_fallocate");\n'
                         "                *text.get() = map(total as usize")
    elif name == "six-workers":
        source = replace(source, "const THREADS: usize = 12;", "const THREADS: usize = 6;")
    elif name == "release-scratch":
        source = replace(source, "if barrier.wait().is_leader() {\n            let mut total",
                         "drop((start, errors, latency));\n        if barrier.wait().is_leader() {\n            let mut total")
    elif name == "final-release":
        source = replace(source, "if barrier.wait().is_leader() {\n            let mut total",
                         "if barrier.wait().is_leader() {\n"
                         "            parsers.iter().for_each(|ps| ps.get().release(&finished));\n"
                         "            let mut total")
    elif name == "parse-counts":
        source = replace(source, "struct Parser {",
                         "struct Parser {\n    counts: Vec<Vec<[u32; 2]>>,")
        source = replace(source, "Parser { local: Interner::new(),",
                         "Parser { counts: vec![vec![]; HOURS], local: Interner::new(),")
        source = replace(source, "p = eol.add(1);",
                         """p = eol.add(1);
            let counts = &mut self.counts[h];
            if counts.is_empty() {
                counts.resize(2048, [0; 2]);
            }
            let count = &mut counts[(key & ID_MASK) as usize];
            count[0] += 1;
            count[1] += (key & ERR != 0) as u32;""")
        source = replace(source, """for ps in &parsers {
                ps.get().each(h, |e, err, _| (start[e + 1], errors[e]) = (start[e + 1] + 1, errors[e] + err as u32));
            }""", """for ps in &parsers {
                let ps = ps.get();
                for (id, &[count, err]) in ps.counts[h].iter().enumerate() {
                    if count != 0 {
                        let e = ps.rank[id] as usize;
                        start[e + 1] += count;
                        errors[e] += err;
                    }
                }
            }""")
    elif name == "fstat":
        source = replace(source, "use std::ffi::c_void;",
                         "use std::ffi::c_void;\nuse std::os::fd::FromRawFd;")
        source = replace(source, 'let size = std::fs::metadata("/data/requests.log").expect("stat").len() as usize;',
                         'let file = unsafe { std::fs::File::from_raw_fd(input) };\n'
                         '    let size = file.metadata().expect("fstat").len() as usize;')
    elif name == "parse-only":
        source = replace(source,
                         '    let out = unsafe { open(c"/out/summary.txt".as_ptr(), 0o1102, 0o644) };\n', "")
        source = replace(source,
                         'assert!(input >= 0 && out >= 0, "open failed"); // O_RDWR | O_CREAT | O_TRUNC',
                         'assert!(input >= 0, "open failed");')
        source = source[:source.index("    let names = Shared::new")] + """
    std::thread::scope(|s| {
        for t in 0..THREADS {
            let parsers = &parsers;
            let cut = &cut;
            s.spawn(move || unsafe {
                parsers[t].get().parse(log.as_ptr().add(cut[t]), log.as_ptr().add(cut[t + 1]));
            });
        }
    });
}
"""
    elif name not in ("baseline", "strip", "crt-static"):
        raise ValueError(name)
    return source


def count_source(context, source, dockerfile):
    (context / "formatted.rs").write_text(source, encoding="utf-8")
    (context / "counted-Dockerfile").write_bytes(dockerfile)
    formatter = run(["docker", "create", "--network=none", "--user", "0:0", "--entrypoint", "sh", TOOLS,
                     "-c", "/opt/rustfmt/bin/rustfmt --edition 2021 /tmp/formatted.rs"
                     " && scc --no-config --no-gitignore --no-ignore --no-scc-ignore"
                     " --no-gitmodule --by-file --format json /tmp"])
    try:
        run(["docker", "cp", context / "formatted.rs", f"{formatter}:/tmp/formatted.rs"])
        run(["docker", "cp", context / "counted-Dockerfile", f"{formatter}:/tmp/Dockerfile"])
        counts = json.loads(run(["docker", "start", "--attach", formatter]))
        run(["docker", "cp", f"{formatter}:/tmp/formatted.rs", context / "formatted.rs"])
    finally:
        run(["docker", "rm", formatter])
    return sum(language["Code"] for language in counts)


def build(name, directory, source, tag, base_ref):
    context = directory / name
    context.mkdir()
    generated = variant(source, name)
    (context / "main.rs").write_text(generated, encoding="utf-8")
    lines = count_source(context, generated, source_at(base_ref, "a", "Dockerfile"))
    (context / "probe.rs").write_bytes((ROOT / "scripts" / "track_a_probe.rs").read_bytes())
    (context / "checks.rs").write_bytes((ROOT / "tests" / "track_a_probe_checks.rs").read_bytes())
    flags = {"strip": " -C strip=symbols", "crt-static": " -C target-feature=+crt-static"}.get(name, "")
    dockerfile = f"""FROM {BUILDER} AS build
WORKDIR /app
COPY main.rs formatted.rs probe.rs checks.rs ./
RUN rustc --edition=2021 -C opt-level=3 -C lto=fat -C target-cpu=native -C panic=abort -C codegen-units=1{flags} --emit=link,asm -o solution main.rs
RUN rustc --edition=2021 --test -C opt-level=2 -C target-cpu=native checks.rs -o checks && ./checks
RUN rustc --edition=2021 -C opt-level=2 -C panic=abort -C strip=symbols -o probe probe.rs
FROM {RUNTIME}
COPY --from=build /app/solution /app/solution.s /app/formatted.rs /app/probe /app/
ENTRYPOINT ["/app/probe"]
"""
    (context / "Dockerfile").write_text(dockerfile, encoding="utf-8")
    run(["docker", "build", "--network=none", "-t", tag, context])
    container = run(["docker", "create", tag])
    try:
        run(["docker", "cp", f"{container}:/app/formatted.rs", context / "formatted.rs"])
        run(["docker", "cp", f"{container}:/app/solution.s", context / "solution.s"])
    finally:
        run(["docker", "rm", container])
    return lines


def measure(tag, data_volume, out_volume, parse_only=False, track="a"):
    output = check.OUTPUT[track]
    run([
        "docker", "run", "--rm", "--network=none", "--entrypoint", "sh",
        "-v", f"{out_volume}:/out:rw", RUNTIME, "-c",
        f'rm -f -- /out/{output} && test -z "$(ls -A /out)"',
    ])
    args = [
        "docker", "run", "--rm", f"--cpuset-cpus={check.CPUSET_CPUS}",
        f"--cpuset-mems={check.CPUSET_MEMS}", "--network=none", "--read-only",
        "--tmpfs", "/tmp:rw,size=1g,exec", "--memory=8g", "--memory-swap=8g",
        "--pids-limit=512", "--cap-drop=ALL", "--security-opt=no-new-privileges",
        "--user", "65534:65534", "--ulimit", "nofile=8192:8192",
        "-v", f"{data_volume}:/data:ro", "-v", f"{out_volume}:/out:rw", tag,
    ]
    record = json.loads(run(args).splitlines()[-1])
    if parse_only:
        run([
            "docker", "run", "--rm", "--network=none", "--entrypoint", "sh",
            "-v", f"{out_volume}:/out:ro", RUNTIME, "-c", 'test -z "$(ls -A /out)"',
        ])
        record["sha256"] = None
        return record
    digest = run([
        "docker", "run", "--rm", "--network=none", "--entrypoint", "sh",
        "-v", f"{out_volume}:/out:ro", RUNTIME, "-c",
        f'test "$(ls -A /out)" = "{output}" && sha256sum /out/{output}',
    ]).split()[0]
    if digest != official_data.TRACKS[track]["sha256"]:
        raise RuntimeError(f"{tag}: incorrect official output: {digest}")
    record["sha256"] = digest
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--variants", nargs="+", choices=VARIANTS, default=list(VARIANTS))
    parser.add_argument("--runs", type=int, default=9)
    parser.add_argument("--base-ref", default=BASELINE_REF,
                        help="Immutable experiment baseline; does not read modified worktree source")
    parser.add_argument("--out-fs", choices=["tmpfs", "disk"], default="tmpfs")
    parser.add_argument("--paired", action="store_true",
                        help="Measure an adjacent baseline for every candidate, alternating AB/BA")
    parser.add_argument("--output", type=Path, required=True, help="Persistent JSON report outside track-a")
    args = parser.parse_args()
    if args.runs < 3 or len(set(args.variants)) != len(args.variants):
        parser.error("Use at least three runs and unique variant names")
    if "baseline" not in args.variants:
        parser.error("Include baseline for paired comparisons")
    if args.paired and len(args.variants) < 2:
        parser.error("--paired requires at least one candidate besides baseline")
    if args.output.resolve().is_relative_to((ROOT / "track-a").resolve()):
        parser.error("Write reports outside the scored track-a directory")
    data = official_data.verify("a")
    source = source_at(args.base_ref, "a", "main.rs").decode("utf-8")
    prefix = f"sd-probe-a-{uuid.uuid4().hex[:12]}"
    volumes, containers, images = [], [], []
    report = {
        "commit": run(["git", "rev-parse", "HEAD"]),
        "base_ref": args.base_ref,
        "source_sha256": hashlib.sha256(source.encode()).hexdigest(),
        "cpuset": check.CPUSET_CPUS,
        "paired": args.paired,
        "out_fs": args.out_fs,
        "fresh_output": True,
        "formatter": run(["docker", "run", "--rm", "--network=none",
                          "--entrypoint", "/opt/rustfmt/bin/rustfmt", TOOLS, "--version"]),
        "docker": {k: v for k, v in json.loads(
            run(["docker", "info", "--format", "{{json .}}"])
        ).items() if k in ("NCPU", "MemTotal", "KernelVersion", "OperatingSystem")},
        "variants": {},
        "order": [],
    }
    (ROOT / ".out").mkdir(exist_ok=True)
    args.output.parent.mkdir(parents=True, exist_ok=True)

    def save():
        args.output.write_text(json.dumps(report, indent=2), encoding="utf-8")

    try:
        with TemporaryDirectory(prefix=prefix, dir=ROOT / ".out") as temp:
            directory = Path(temp)
            for name in args.variants:
                tag = f"{prefix}-{name}"
                lines = build(name, directory, source, tag, args.base_ref)
                images.append(tag)
                report["variants"][name] = {"lines": lines, "regression_tests_passed": True, "runs": []}
                print(f"Built {name}: {lines} formatted scc code lines", flush=True)
            tag = images[args.variants.index("baseline")]
            report["environment"] = run([
                "docker", "run", "--rm", "--network=none", "--cap-drop=ALL",
                "--security-opt=no-new-privileges", "--user", "65534:65534", tag, "environment",
            ])
            data_volume, out_volume = f"{prefix}-data", f"{prefix}-out"
            for volume in (data_volume, out_volume):
                options = (["--driver", "local", "--opt", "type=tmpfs",
                            "--opt", "device=tmpfs", "--opt", "o=size=1g,mode=0777"]
                           if volume == out_volume and args.out_fs == "tmpfs" else [])
                run(["docker", "volume", "create", *options, volume])
                volumes.append(volume)
            stager = run([
                "docker", "create", "--network=none", "--entrypoint", "sh",
                "-v", f"{data_volume}:/data", "-v", f"{out_volume}:/out",
                RUNTIME, "-c", "sleep 3600",
            ])
            containers.append(stager)
            run(["docker", "start", stager])
            run(["docker", "cp", str(data / "requests.log"), f"{stager}:/data/requests.log"])
            run(["docker", "exec", stager, "sh", "-c",
                 "chmod 755 /data; chmod 644 /data/requests.log; chmod 777 /out"])
            digest = run(["docker", "exec", stager, "sha256sum", "/data/requests.log"]).split()[0]
            if digest != official_data.TRACKS["a"]["inputs"]["requests.log"]:
                raise RuntimeError("Staged input hash mismatch")
            report["mapping_probes"] = {}
            for mode in ("baseline", "populate", "hugepage"):
                run(["docker", "exec", stager, "sh", "-c", "cat /data/requests.log >/dev/null"])
                report["mapping_probes"][mode] = run([
                    "docker", "run", "--rm", "--network=none", "--read-only",
                    f"--cpuset-cpus={check.CPUSET_CPUS}", "--cap-drop=ALL",
                    "--security-opt=no-new-privileges", "--user", "65534:65534",
                    "-v", f"{data_volume}:/data:ro", tag, "mapping", mode,
                ])
            for repetition in range(args.runs + 1):
                # First round is warmup, never included in the estimates.
                order = args.variants.copy()
                random.Random(repetition).shuffle(order)
                if args.paired:
                    order.remove("baseline")
                    order = [item for i, name in enumerate(order) for item in (
                        ("baseline", name) if (repetition + i) % 2 == 0 else (name, "baseline")
                    )]
                report["order"].append(order)
                previous = None
                for index, name in enumerate(order):
                    # Warm the host cache OUTSIDE the timed child and its cgroup.
                    run(["docker", "exec", stager, "sh", "-c", "cat /data/requests.log >/dev/null"])
                    record = measure(f"{prefix}-{name}", data_volume, out_volume, name == "parse-only")
                    if repetition:
                        report["variants"][name]["runs"].append(record)
                        if args.paired and index % 2:
                            control, candidate = ((record, previous) if name == "baseline"
                                                  else (previous, record))
                            candidate["control"] = control.copy()
                    print(f"{repetition:02} {name:18} {record['ns'] / 1e6:8.3f} ms "
                          f"{record['peak_bytes'] / 2**20:6.2f} MiB {record['minor_faults']:6} faults", flush=True)
                    save()
                    previous = record
            official_data.verify("a")
    finally:
        check.cleanup_docker(containers, volumes)
        if images:
            run(["docker", "image", "rm", *images])
    base = report["variants"]["baseline"]
    for name, result in report["variants"].items():
        controls = [r["control"] for r in result["runs"]] if args.paired and name != "baseline" else base["runs"]
        ratios = [r["ns"] / b["ns"] for r, b in zip(result["runs"], controls)]
        result["median_ms"] = statistics.median(r["ns"] for r in result["runs"]) / 1e6
        result["median_peak_mib"] = statistics.median(r["peak_bytes"] for r in result["runs"]) / 2**20
        result["paired_time_ratio"] = statistics.median(ratios)
        result["paired_ratio_range"] = [min(ratios), max(ratios)]
        result["paired_time_ratio_bootstrap_95"] = median_interval(ratios)
        if name != "parse-only":
            # Dependency weight is unchanged; source benefit is capped at reference 56.
            k_ratios = [
                (b["ns"] / r["ns"]) ** 0.5 *
                (min(228e6 / r["peak_bytes"], 50) / min(228e6 / b["peak_bytes"], 50)) ** 0.2 *
                (max(base["lines"], 56) / max(result["lines"], 56)) ** 0.15
                for r, b in zip(result["runs"], controls)
            ]
            result["estimated_k_ratio"] = statistics.median(k_ratios)
            result["estimated_k_ratio_bootstrap_95"] = median_interval(k_ratios)
            result["estimated_machine_points_delta"] = 50 * math.log10(result["estimated_k_ratio"])
    save()
    print(json.dumps({name: {k: v for k, v in result.items() if k != "runs"}
                      for name, result in report["variants"].items()}, indent=2))


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, official_data.DatasetError) as exc:
        sys.exit(f"FAIL: {exc}")
