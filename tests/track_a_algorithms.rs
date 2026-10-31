// Root-level Rust regression suite: NOT part of track-a's scored source folder.
// Run: rustc --edition=2021 -C target-cpu=native --test tests/track_a_algorithms.rs
//      -o /tmp/track-a-tests && /tmp/track-a-tests
mod submission {
    #![allow(dead_code)]
    include!("../track-a/main.rs");

    #[cfg(test)]
    mod tests {
        use super::*;

        fn rng(seed: &mut u64) -> u32 {
            *seed ^= *seed << 13;
            *seed ^= *seed >> 7;
            *seed ^= *seed << 17;
            (*seed >> 16) as u32
        }

        fn oracle(input: &[u32]) -> [u32; 3] {
            let mut sorted = input.to_vec();
            sorted.sort_unstable();
            let n = sorted.len();
            [50usize, 95, 99].map(|p| sorted[(p * n + 99) / 100 - 1])
        }

        #[test]
        fn exact_percentiles_match_full_sort() {
            if !is_x86_feature_detected!("avx2") {
                return;
            }
            let mut seed = 0x72ae_2026_deed_0001_u64;
            for n in 1..=512 {
                for experiment in 0..48 {
                    let mut values: Vec<u32> = (0..n)
                        .map(|_| match experiment % 6 {
                            0 => rng(&mut seed),
                            1 => rng(&mut seed) % 16,
                            2 => u32::MAX - (rng(&mut seed) % 100),
                            3 => 0,
                            4 => u32::MAX,
                            _ => rng(&mut seed) % 3_100_000,
                        })
                        .collect();
                    if experiment % 8 == 7 {
                        values.sort_unstable_by(|a, b| b.cmp(a));
                    }
                    let expected = oracle(&values);
                    let actual = unsafe { exact_percentiles(&mut values) };
                    assert_eq!(actual, expected, "n={n} experiment={experiment}");
                }
            }
        }

        #[test]
        fn high_volume_radix_select_matches_sort() {
            let mut seed = 0x45bd_a11c_20de_9999_u64;
            for &n in &[65usize, 127, 256, 513, 1024, 4096, 9223] {
                for distribution in 0..12 {
                    let values: Vec<u32> = (0..n)
                        .map(|_| {
                            let v = rng(&mut seed);
                            if distribution & 1 == 0 { v } else { v % 3000 }
                        })
                        .collect();
                    let ranks = [50usize, 95, 99].map(|p| (p * n + 99) / 100 - 1);
                    assert_eq!(radix_percentiles(&values, ranks), oracle(&values));
                }
            }
        }

        #[test]
        fn simd_bitonic_network_matches_sort() {
            if !is_x86_feature_detected!("avx2") {
                return;
            }
            let mut seed = 0x1234_5678_9abc_def0_u64;
            for n in 8..=32 {
                for case in 0..256 {
                    let mut values: Vec<u32> = (0..n)
                        .map(|_| match case % 4 {
                            0 => rng(&mut seed),
                            1 => rng(&mut seed) % 5,
                            2 => u32::MAX - rng(&mut seed) % 5,
                            _ => rng(&mut seed) % 1_000_000,
                        })
                        .collect();
                    let mut expected = values.clone();
                    expected.sort_unstable();
                    unsafe { bitonic_sort_small(&mut values) };
                    assert_eq!(values, expected, "n={n} case={case}");
                }
            }
        }
    }
}
