#!/usr/bin/env python3
"""Ensure a failed browser generation cannot expose a stale link source list."""
import argparse
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

parser = argparse.ArgumentParser()
parser.add_argument("--upstream", required=True, type=Path)
args = parser.parse_args()
sys.path.insert(0, str(args.upstream.resolve() / "port/tools"))
import prepare

class FailedGenerationGate(unittest.TestCase):
    def run_incomplete_preparation(self, backend):
        with tempfile.TemporaryDirectory() as work:
            out = Path(work)
            marker = out / "sources.cmake"
            marker.write_text("# a previous completed source list\n")
            argv = ["prepare.py", "--out", str(out), "--preprocessor", backend]
            # An incomplete source tree fails before reference generation. A
            # fresh browser attempt must not retain the previous link plan.
            with patch.object(sys, "argv", argv), patch.object(prepare, "load_modules", return_value=[]):
                with self.assertRaisesRegex(SystemExit, "no function at"):
                    prepare.main()
            return marker.exists()

    def test_failed_wasm_attempt_removes_the_old_link_plan(self):
        self.assertFalse(self.run_incomplete_preparation("clang"))

    def test_default_native_path_preserves_existing_behavior(self):
        self.assertTrue(self.run_incomplete_preparation("msvc"))

if not unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(FailedGenerationGate)).wasSuccessful():
    raise SystemExit(1)
