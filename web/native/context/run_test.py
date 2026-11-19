#!/usr/bin/env python3
"""Compile and execute the genuine Asyncify context regression, ROM-free."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
from verify_upstream import verify, verify_emcc

parser = argparse.ArgumentParser()
parser.add_argument("--upstream", required=True, type=Path)
parser.add_argument("--emcc", default="emcc")
parser.add_argument("--node", default="node")
args = parser.parse_args()
verify(args.upstream)
verify_emcc(args.emcc)
with tempfile.TemporaryDirectory(prefix="days-fiber-wasm-") as directory:
    for optimization in ("-O1", "-O2"):
        executable = Path(directory) / "context-smoke.js"
        subprocess.run([
            args.emcc, "-std=c11", optimization, "-Wall", "-Wextra", "-Werror", "-sASYNCIFY",
            "-sASSERTIONS=2", "-sSTACK_OVERFLOW_CHECK=2", "-sALLOW_MEMORY_GROWTH=1",
            "-I", str(args.upstream.resolve() / "port/host/app"),
            str(HERE / "context_emscripten.c"), str(HERE / "tests/context_smoke.c"),
            "-o", str(executable),
        ], check=True)
        subprocess.run([args.node, str(executable)], check=True)
