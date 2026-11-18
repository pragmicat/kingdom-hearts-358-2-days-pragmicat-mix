#!/usr/bin/env python3
"""Exercise the recovered compiler backend through the actual reference pass.

Only temporary fixture/generated C is written; no ROM or extracted game data.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument("--upstream", required=True, type=Path)
parser.add_argument("--emcc", required=True)
parser.add_argument("--node", default="node")
args = parser.parse_args()
upstream = args.upstream.resolve()
sys.path.insert(0, str(upstream / "port/tools"))
from references import build
from references.preprocess import Flags

class PreprocessorContract(unittest.TestCase):
    def test_default_msvc_arguments_remain_native(self):
        flags = Flags(includes=[Path("include")], defines=["DAYS=1"], forced=[Path("forced.h")])
        self.assertEqual(flags.arguments(), ["/Iinclude", "/DDAYS=1", "/FIforced.h"])
        self.assertEqual(flags.arguments("clang"), ["-Iinclude", "-DDAYS=1", "-include", "forced.h"])

    def test_clang_and_msvc_line_markers_track_real_dependencies(self):
        text = '#line 3 "native.h"\n# 1 "browser.h" 1\n# 1 "<built-in>" 3\n# 1 "<command line>" 1\n'
        self.assertEqual(build._sources_of(text), ["browser.h", "native.h"])

    def test_mixed_target_profiles_are_rejected_before_generation(self):
        with tempfile.TemporaryDirectory() as work:
            with self.assertRaisesRegex(ValueError, "together"):
                build.rewrite_all([], build.Program({}, {}), Path(work), backend="clang", abi_profile="windows-x64")

    def test_actual_preprocessing_tracks_and_invalidates_changed_headers(self):
        with tempfile.TemporaryDirectory() as work:
            directory = Path(work)
            cache = directory / "cache"
            cache.mkdir()
            source, header = directory / "input.c", directory / "dependency.h"
            source.write_text('#include "dependency.h"\nint combat_probe(void) { return DAYS_VALUE; }\n')
            header.write_text("#define DAYS_VALUE 7\n")
            build._init(str(cache), build.tools_key(), "x64", "clang", args.emcc, "wasm32")
            result = build._definitions(str(source))
            self.assertEqual(result[-1], "")
            self.assertIn("combat_probe", result[1])
            meta = build._meta(str(source))
            self.assertIsNotNone(meta)
            self.assertIn(str(header), meta["files"])
            self.assertTrue(meta["tools"].endswith("|abi=wasm32"))
            original_flags = build._flags_key
            # The ABI fingerprint belongs to the AST/emission layer. It must
            # not require rereading unchanged preprocessed cartridge source.
            build._init(str(cache), build.tools_key(), "x64", "clang", args.emcc, "windows-x64")
            self.assertEqual(build._flags_key, original_flags)
            self.assertNotEqual(meta["tools"], build._tools)
            header.write_text("#define DAYS_VALUE 700\n")
            build._stats.clear()  # equivalent to the next fresh worker process
            self.assertIsNone(build._meta(str(source)))

if not unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(PreprocessorContract)).wasSuccessful():
    raise SystemExit(1)

# Feed an ORIGINAL native Days battle translation unit through rewrite_all,
# including worker initialization, cached preprocessor, wasm32 shape analysis,
# whole-program index, emission and generated data-area header.
relative = "src/overlays/field/ov022_battle/Ov022_IsFrameInComboWindow.c"
source = upstream / "external/khdays-decomp" / relative
with tempfile.TemporaryDirectory(prefix="days-compiler-step-") as work:
    directory = Path(work)
    output = directory / "rewritten" / relative
    output.parent.mkdir(parents=True)
    rewritten, area = build.rewrite_all(
        [(str(source), str(output))], build.Program({}, {"Ov022_IsFrameInComboWindow":0x02000000}),
        directory, processes=1, backend="clang", executable=args.emcc, abi_profile="wasm32")
    if rewritten != [str(output)] or not (directory / "khdays_data_area.h").is_file():
        raise AssertionError("The actual reference pass did not finish its output/header.")
    executable = directory / "original-combo.cjs"
    subprocess.run([
        args.emcc, "-std=c17", "-O2", "-fms-extensions", "-fpack-struct=4",
        "-Wno-ignored-attributes", "-Wno-unknown-pragmas", "-Wno-parentheses-equality",
        "-I", str(upstream / "port/compat"), "-I", str(directory),
        str(output), str(HERE / "original_combo_test.c"), "-sENVIRONMENT=node", "-sSINGLE_FILE=1",
        "-sWASM_ASYNC_COMPILATION=0", "-sEXIT_RUNTIME=1", "-sASSERTIONS=2", "-sALLOW_MEMORY_GROWTH=1",
        "-o", str(executable),
    ], check=True)
    subprocess.run([args.node, str(executable)], check=True)
print(json.dumps({"original_game_translation_units":1,"whole_game_generation":False,
                  "backend":"clang","abi_profile":"wasm32","emscripten":"4.0.23"}))
