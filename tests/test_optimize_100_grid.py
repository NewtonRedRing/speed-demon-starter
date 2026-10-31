"""Lightweight invariant checks for the four 100-trial search grids.

Run: python3 -m unittest tests.test_optimize_100_grid
No dataset or Docker required; this verifies configuration completeness only.
"""
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "optimize_100", ROOT / "scripts" / "optimize_100.py")
optimizer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(optimizer)


class TestHundredTrialGrid(unittest.TestCase):
    def test_exactly_100_distinct_configs_per_track(self):
        for track in "abcd":
            with self.subTest(track=track):
                grid = optimizer.choices(track)
                self.assertEqual(len(grid), 100)
                self.assertEqual(len(set(grid)), 100)
                self.assertIn((12, optimizer.BASELINE_PARAM[track], "base"),
                              grid)

    def test_workload_defaults_still_match_submitted_code(self):
        patterns = {
            "a": "records.reserve((end - begin) / SD_TUNE_PARAM + 1)",
            "b": "next_term.fetch_add(SD_TUNE_PARAM,",
            "c": "std::atomic<Entry*>, SD_TUNE_PARAM>",
            "d": "buffer(SD_TUNE_PARAM * 1024)",
        }
        for track, snippet in patterns.items():
            source = (ROOT / f"track-{track}" / "src" / "main.cpp").read_text()
            dockerfile = (ROOT / f"track-{track}" / "Dockerfile").read_text()
            with self.subTest(track=track):
                self.assertIn(snippet, source)
                self.assertIn("#define SD_TUNE_WORKERS 12", source)
                self.assertIn("#define SD_TUNE_PARAM", source)
                self.assertIn("ARG SD_TUNE_WORKERS=12", dockerfile)
                self.assertIn(
                    f"ARG SD_TUNE_PARAM={optimizer.BASELINE_PARAM[track]}",
                    dockerfile)

    def test_score_proxy_rewards_time_and_memory_reduction(self):
        score = optimizer.proxy
        self.assertGreater(score(0.05, 20_000_000),
                           score(0.10, 20_000_000))
        self.assertGreater(score(0.05, 20_000_000),
                           score(0.05, 40_000_000))


if __name__ == "__main__":
    unittest.main()
