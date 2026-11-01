import importlib.util
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("probe", ROOT / "scripts" / "investigate_track_a.py")
probe = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(probe)
sys.path.insert(0, str(ROOT / "scripts"))
import investigate_tracks


class ProbeTests(unittest.TestCase):
    def test_every_variant_is_independent_and_preserves_input_path(self):
        source = probe.source_at(probe.BASELINE_REF, "a", "main.rs").decode("utf-8")
        for name in probe.VARIANTS:
            with self.subTest(name=name):
                generated = probe.variant(source, name)
                self.assertEqual(generated.count('c"/data/requests.log"'), 1)
                self.assertEqual(generated.count("fn main()"), 1)
                self.assertEqual(generated, probe.variant(source, name))
                if name == "parse-only":
                    self.assertNotIn('c"/out/', generated)
                if name in ("baseline", "strip", "crt-static"):
                    self.assertEqual(generated, source)
                else:
                    self.assertNotEqual(generated, source)

    def test_source_anchors_fail_closed(self):
        with self.assertRaises(RuntimeError):
            probe.replace("twice twice", "twice", "once")
        with self.assertRaises(RuntimeError):
            probe.replace("missing", "anchor", "replacement")
        with self.assertRaises(ValueError):
            probe.variant("", "unknown")

    def test_bootstrap_is_deterministic_and_bounded(self):
        self.assertEqual(probe.median_interval([1.0] * 9), [1.0, 1.0])
        values = [0.8, 0.9, 1.0, 1.1, 1.2]
        interval = probe.median_interval(values)
        self.assertEqual(interval, probe.median_interval(values))
        self.assertGreaterEqual(interval[0], min(values))
        self.assertLessEqual(interval[1], max(values))
        self.assertLessEqual(interval[0], interval[1])

    def test_all_track_variants_have_exact_source_anchors(self):
        for track, names in investigate_tracks.OPTIONS.items():
            source = probe.source_at(probe.BASELINE_REF, track, "main.rs").decode("utf-8")
            for name in names:
                with self.subTest(track=track, variant=name):
                    generated = investigate_tracks.variant(track, name, source)
                    self.assertNotEqual(generated, source)
                    self.assertEqual(generated.count("fn main()"), 1)
                    self.assertEqual(generated, investigate_tracks.variant(track, name, source))

    def test_measure_requires_fresh_output_and_exact_hash(self):
        digest = probe.official_data.TRACKS["a"]["sha256"]
        with patch.object(probe, "run", side_effect=["", '{"ns": 1}', f"{digest} output"]) as run:
            self.assertEqual(probe.measure("tag", "input", "output")["sha256"], digest)
        cleanup, execution, verification = [call.args[0] for call in run.call_args_list]
        self.assertIn(f"rm -f -- /out/{probe.check.OUTPUT['a']}", cleanup[-1])
        self.assertIn('test -z "$(ls -A /out)"', cleanup[-1])
        self.assertIn("output:/out:rw", execution)
        self.assertIn("sha256sum", verification[-1])

    def test_measure_rejects_missing_or_incorrect_output(self):
        for result in [RuntimeError("missing output"), "wronghash output"]:
            with self.subTest(result=result):
                with patch.object(probe, "run", side_effect=["", '{"ns": 1}', result]):
                    with self.assertRaises(RuntimeError):
                        probe.measure("tag", "input", "output")

    def test_parse_only_requires_no_output(self):
        with patch.object(probe, "run", side_effect=["", '{"ns": 1}', ""]) as run:
            self.assertIsNone(probe.measure("tag", "input", "output", parse_only=True)["sha256"])
        self.assertEqual(run.call_args_list[-1].args[0][-1], 'test -z "$(ls -A /out)"')


if __name__ == "__main__":
    unittest.main()
