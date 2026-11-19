#!/usr/bin/env python3
"""Exercise the checked compact mapping with real Asyncify game stacks."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
NATIVE = HERE.parent.parent
sys.path.insert(0, str(NATIVE))
from verify_upstream import verify, verify_emcc

parser = argparse.ArgumentParser()
parser.add_argument("--upstream", required=True, type=Path)
parser.add_argument("--emcc", default="emcc")
parser.add_argument("--node", default="node")
args = parser.parse_args()
verify(args.upstream)
verify_emcc(args.emcc)
with tempfile.TemporaryDirectory(prefix="days-compact-window-wasm-") as directory:
    executable = Path(directory) / "window-smoke.js"
    subprocess.run([
        args.emcc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-sASYNCIFY", "-sASSERTIONS=2",
        "-sSTACK_OVERFLOW_CHECK=2", "-sALLOW_MEMORY_GROWTH=1", "-I", str(HERE),
        "-I", str(args.upstream.resolve() / "port/host/app"),
        str(HERE / "compact_window.c"), str(NATIVE / "context/context_emscripten.c"),
        str(HERE / "tests/window_smoke.c"), "-o", str(executable),
    ], check=True)
    subprocess.run([args.node, str(executable)], check=True)
