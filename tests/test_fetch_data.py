"""Dataset downloads must verify content before replacing existing inputs."""
import hashlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import fetch_data


class Downloads(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.path = Path(self.directory.name) / "input.txt"
        self.payload = b"official bytes\n"
        self.expected = hashlib.sha256(self.payload).hexdigest()

    def test_verified_input_is_reused_without_network(self):
        self.path.write_bytes(self.payload)
        with patch.object(fetch_data.urllib.request, "urlopen") as request:
            fetch_data.fetch(self.path, "https://example.invalid/data", self.expected)
            request.assert_not_called()

    def test_download_replaces_invalid_input_only_after_hash_passes(self):
        self.path.write_bytes(b"old")
        with patch.object(fetch_data.urllib.request, "urlopen", return_value=io.BytesIO(self.payload)):
            fetch_data.fetch(self.path, "https://example.invalid/data", self.expected)
        self.assertEqual(self.path.read_bytes(), self.payload)
        self.assertEqual(list(self.path.parent.iterdir()), [self.path])

    def test_wrong_hash_preserves_old_input_and_cleans_temporary_file(self):
        self.path.write_bytes(b"old")
        with patch.object(fetch_data.urllib.request, "urlopen", return_value=io.BytesIO(b"wrong")):
            with self.assertRaisesRegex(RuntimeError, "SHA-256 mismatch"):
                fetch_data.fetch(self.path, "https://example.invalid/data", self.expected)
        self.assertEqual(self.path.read_bytes(), b"old")
        self.assertEqual(list(self.path.parent.iterdir()), [self.path])

    def test_network_failure_is_not_reported_as_success(self):
        with patch.object(fetch_data.urllib.request, "urlopen", side_effect=TimeoutError("timeout")):
            with self.assertRaises(TimeoutError):
                fetch_data.fetch(self.path, "https://example.invalid/data", self.expected)
        self.assertEqual(list(self.path.parent.iterdir()), [])

    def test_input_verification_rejects_modified_and_missing_files(self):
        manifest = {"tracks": {"a": {"inputs_sha256": {self.path.name: self.expected}}}}
        with self.assertRaisesRegex(RuntimeError, "official input missing"):
            fetch_data.verify_inputs("a", self.path.parent, manifest)
        self.path.write_bytes(self.payload)
        fetch_data.verify_inputs("a", self.path.parent, manifest)
        self.path.write_bytes(b"modified")
        with self.assertRaisesRegex(RuntimeError, "SHA-256 mismatch"):
            fetch_data.verify_inputs("a", self.path.parent, manifest)

    def test_manifest_validation_rejects_unofficial_output_hash(self):
        manifest = {
            "seed": 1,
            "tracks": {
                track: {
                    "inputs_sha256": {name: self.expected for name in names},
                    "output": fetch_data.OUTPUTS[track],
                    "expected_output_sha256": [self.expected],
                }
                for track, names in fetch_data.INPUTS.items()
            },
        }
        payload = json.dumps(manifest).encode()
        with patch.object(fetch_data.urllib.request, "urlopen", return_value=io.BytesIO(payload)):
            self.assertEqual(fetch_data.load_manifest(), manifest)
        manifest["tracks"]["a"]["expected_output_sha256"] = ["invalid"]
        with patch.object(fetch_data.urllib.request, "urlopen",
                          return_value=io.BytesIO(json.dumps(manifest).encode())):
            with self.assertRaisesRegex(RuntimeError, "invalid official SHA-256"):
                fetch_data.load_manifest()


if __name__ == "__main__":
    unittest.main()
