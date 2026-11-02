## Oracle experiment submission

- Track: A / B / C / D
- Baseline approved `speed-demon` SHA:
- Exact candidate SHA:
- One-line algorithmic hypothesis:
- Modified files:
- Safety invariants and assumptions:

### Correctness (mandatory before promotion)
- [ ] Rust compile passed
- [ ] `python3 check.py <track> --data <official-full-seed1-path> --expected <organizer-published-SHA256>` passed
- [ ] Alternative seeds and adversarial cases passed (describe):
- [ ] No runtime dependencies, baked-in datasets, contest-rule violations or sandbox failures

### Performance (mandatory before promotion)
- Benchmark command / runtime hardware:
- Baseline process-time median of >=5 runs:
- Candidate process-time median of >=5 runs:
- Baseline / candidate peak memory:
- Baseline / candidate source size:
- Official score + run URL + exact submission SHA, **if available**:
- Unresolved regressions:

**Requested Oracle decision:** HOLD / REVIEW / PROMOTE

> Never push directly to `speed-demon`. An unverified PR stays draft. Scope CI is not an official dataset validation.
