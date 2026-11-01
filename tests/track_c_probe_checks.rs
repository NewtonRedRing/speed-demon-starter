mod submission {
    #![allow(dead_code)]
    include!("checked-main.rs");

    #[cfg(test)]
    mod checks {
        use super::*;

        fn padded(s: &[u8]) -> &'static [u8] {
            let mut bytes = s.to_vec();
            bytes.extend_from_slice(&[0; 64]);
            &Box::leak(bytes.into_boxed_slice())[..s.len()]
        }

        #[test]
        fn exact_names_survive_forced_hash_and_prefix_collisions() {
            let names = Names::new(2);
            let values: Vec<_> = [
                "",
                "A",
                "A ",
                "Same Prefix Here",
                "Same Prefix Here Tail",
                "Same Prefix Here Tails",
                "Same Prefix Here Tail ",
                "Same Prefix Here Other",
                "Ann \"\"Annie\"\" Lee",
            ]
            .iter()
            .map(|s| padded(s.as_bytes()))
            .collect();
            let ids: Vec<_> = values
                .iter()
                .map(|s| names.number(7, s.as_ptr() as usize, s.len()))
                .collect();
            let mut unique = ids.clone();
            unique.sort_unstable();
            unique.dedup();
            assert_eq!(unique.len(), values.len());
            for (s, id) in values.iter().zip(ids) {
                let copy = padded(s);
                assert_eq!(names.number(7, copy.as_ptr() as usize, copy.len()), id);
            }
        }

        #[test]
        fn concurrent_publication_and_country_membership_are_exact() {
            let names = Names::new(2);
            let value = padded(b"Same Prefix Here Longer Than Sixteen");
            let hash = hash_name(value, 0, value.len());
            std::thread::scope(|scope| {
                for worker in 0..12 {
                    let names = &names;
                    scope.spawn(move || {
                        let mut sightings = Sightings::new(names);
                        for _ in 0..100 {
                            sightings.push(worker % 2, hash, value.as_ptr() as usize, value.len());
                        }
                        sightings.finish();
                    });
                }
            });
            assert_eq!(names.distinct(0), 1);
            assert_eq!(names.distinct(1), 1);
        }
    }
}
