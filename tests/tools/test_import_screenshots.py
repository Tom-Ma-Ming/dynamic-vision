import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from PIL import Image


class IntakeTest(unittest.TestCase):
    def test_missing_empty_and_out_of_bounds(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ("missing", "negative", "bad"):
                Image.new("RGB", (8, 8)).save(root / (name + ".png"))
            (root / "negative.txt").write_text("")
            (root / "bad.txt").write_text("0 0.1 0.1 1 1")
            script = Path(__file__).resolve().parents[2] / "tools/import_screenshots.py"
            result = subprocess.run([sys.executable, script, root, root / "out"], capture_output=True)
            report = json.loads((root / "out/quality.json").read_text())
            self.assertEqual(result.returncode, 1)
            self.assertEqual(report["empty_labels"], 1)
            self.assertEqual(report["invalid_images"], 2)
            self.assertEqual(report["duplicate_image_copies"], 2)
