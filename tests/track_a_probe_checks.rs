// Copied beside each experimental main.rs by investigate_track_a.py.
mod submission {
    #![allow(dead_code)]
    include!("main.rs");

    #[cfg(test)]
    mod probe_checks {
        use super::*;

        fn padded(bytes: &[u8]) -> &'static [u8] {
            let mut storage = bytes.to_vec();
            storage.extend_from_slice(&[0; 64]);
            &Box::leak(storage.into_boxed_slice())[..bytes.len()]
        }

        #[test]
        fn endpoint_ids_are_exact_across_hash_collisions() {
            let mut table = Interner::new();
            let names: Vec<_> = (0..1842)
                .map(|i| padded(format!("/samepre/{i:04}/samesuffix").as_bytes()))
                .collect();
            // Same length, first eight and last eight bytes: identical CRC.
            for (id, &name) in names.iter().enumerate() {
                assert_eq!(table.id(name), id as u32);
            }
            for _ in 0..3 {
                for (id, &name) in names.iter().enumerate().rev() {
                    assert_eq!(table.id(padded(name)), id as u32);
                }
            }
        }

        #[test]
        fn byte_equality_handles_prefixes_and_long_names() {
            for n in 1..=128 {
                let bytes: Vec<_> = (0..n).map(|i| b'a' + (i % 26) as u8).collect();
                let a = padded(&bytes);
                assert!(same(a, padded(&bytes)));
                assert!(!same(a, padded(&[bytes.as_slice(), b"x"].concat())));
                let mut different = bytes;
                different[n / 2] ^= 1;
                assert!(!same(a, padded(&different)));
            }
        }

        #[test]
        fn simd_decimal_covers_all_widths_and_u32_extremes() {
            for value in [
                0u32,
                1,
                9,
                10,
                99,
                100,
                999,
                1000,
                9999,
                10000,
                99999,
                100000,
                999999,
                1000000,
                9999999,
                10000000,
                99999999,
                100000000,
                999999999,
                1000000000,
                u32::MAX,
            ] {
                let digits = value.to_string();
                let mut bytes = vec![b' '; 32];
                bytes.extend_from_slice(digits.as_bytes());
                unsafe {
                    assert_eq!(decode(bytes.as_ptr().add(bytes.len()), digits.len()), value);
                }
            }
        }

        #[test]
        fn scan_fallback_crosses_vector_boundaries() {
            for offset in [0usize, 1, 31, 32, 63, 64, 95, 128] {
                let mut bytes = vec![b'x'; 192];
                bytes[offset] = b'\n';
                unsafe {
                    let p = bytes.as_ptr();
                    assert_eq!(find(false, p, p, b'\n').offset_from(p), offset as isize);
                }
            }
        }

        #[test]
        fn packed_and_wide_records_preserve_hours_errors_and_latencies() {
            let input = padded(
                concat!(
                    "2026-10-30T18:00:00.000000Z GET /a 200 0\n",
                    "2026-10-30T18:01:00.000000Z POST /a 503 1048575\n",
                    "2026-10-30T19:00:00.000000Z GET /b 500 1048576\n",
                    "2026-10-30T19:01:00.000000Z GET /a 404 4294967295\n",
                )
                .as_bytes(),
            );
            let mut parser = Parser::new(input.len());
            unsafe {
                parser.parse(input.as_ptr(), input.as_ptr().add(input.len()));
                parser.rank = (0..parser.local.names.len() as u32).collect();
                let mut first = vec![];
                parser.each(0, |id, error, latency| first.push((id, error, latency)));
                first.sort_unstable();
                assert_eq!(first, [(0, false, 0), (0, true, 1048575)]);
                let mut second = vec![];
                parser.each(1, |id, error, latency| second.push((id, error, latency)));
                second.sort_unstable();
                assert_eq!(second, [(0, false, u32::MAX), (1, true, 1048576)]);
                let finished: Vec<_> = (0..HOURS).map(|_| AtomicBool::new(true)).collect();
                parser.release(&finished);
                assert_eq!(parser.kept, parser.next);
            }
        }
    }
}
