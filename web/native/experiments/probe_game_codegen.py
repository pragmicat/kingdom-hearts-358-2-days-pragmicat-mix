#!/usr/bin/env python3
"""Probe real native combo C -> reference rewrite -> Wasm + explicit hook record.

Generated game C lives only in a temporary directory and is never packaged.
This proves one translation unit, not the whole game's ABI or static registry.
"""
import argparse
import hashlib
import importlib.metadata
import json
from pathlib import Path
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
from verify_upstream import PIN, verify, verify_emcc

parser = argparse.ArgumentParser()
parser.add_argument("--upstream", required=True, type=Path)
parser.add_argument("--emcc", default="emcc")
parser.add_argument("--node", default="node")
args = parser.parse_args()
upstream = args.upstream.resolve()
verify(upstream)
verify_emcc(args.emcc)
if importlib.metadata.version("pycparser") != "3.0":
    raise SystemExit("This probe was validated with pycparser==3.0.")
for relative, expected in PIN["probe_sha256"].items():
    if hashlib.sha256((upstream / relative).read_bytes()).hexdigest() != expected:
        raise SystemExit(f"The codegen probe dependency changed: {relative}")
compiler = Path(subprocess.check_output(["which", args.emcc], text=True).strip() if "/" not in args.emcc else args.emcc).resolve().parent.parent / "bin/clang-22"
source = upstream / "external/khdays-decomp/src/overlays/field/ov022_battle/Ov022_IsFrameInComboWindow.c"
preprocessed = subprocess.check_output([
    str(compiler), "--driver-mode=cl", "/nologo", "/E", "/Zp4", "--", str(source)
], text=True)
sys.path.insert(0, str(upstream / "port/tools"))
from references import rewrite_text
generated = rewrite_text(preprocessed, str(source), routes=True)

with tempfile.TemporaryDirectory(prefix="days-real-codegen-wasm-") as temporary:
    directory = Path(temporary)
    (directory / "combo.c").write_text(generated, encoding="utf-8")
    (directory / "khdays_data_area.h").write_text("/* This translation unit has no static data area. */\n", encoding="utf-8")
    executable = directory / "combo-probe.js"
    subprocess.run([
        args.emcc, "-std=c17", "-O2", "-fdeclspec", "-Wno-ignored-attributes", "-Wno-unknown-pragmas",
        "-I", str(upstream / "port/compat"), "-I", str(directory),
        str(directory / "combo.c"), str(HERE / "tests/combo_codegen_smoke.c"),
        "-sASSERTIONS=2", "-sALLOW_MEMORY_GROWTH=1", "-o", str(executable),
    ], check=True)
    subprocess.run([args.node, str(executable)], check=True)
print(json.dumps({"translation_units":1,"whole_game":False,"upstream":PIN["commit"],
                  "decomp":PIN["decomp_commit"],"emscripten":PIN["emscripten"],"pycparser":"3.0"}))
