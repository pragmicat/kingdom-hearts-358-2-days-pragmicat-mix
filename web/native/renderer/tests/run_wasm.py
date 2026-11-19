#!/usr/bin/env python3
"""Build real renderer Wasm and execute both native-structure and JS regressions."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

here = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(here))
from build_web import build, upstream_sources

parser = argparse.ArgumentParser()
parser.add_argument("--upstream", type=Path, required=True)
parser.add_argument("--emcc", default="emcc")
parser.add_argument("--node", default="node")
parser.add_argument("--output", type=Path, default=here / "build-web")
args = parser.parse_args()
module = build(args.emcc, args.upstream, args.output)
subprocess.run([args.node, str(here / "tests" / "wasm-presenter.test.mjs"), str(module)], check=True)
with tempfile.TemporaryDirectory(prefix="days-renderer-wasm-") as work:
    executable = Path(work) / "renderer-test.cjs"
    subprocess.run([
        args.emcc, "-std=c17", "-O2", "-I", str(args.upstream.resolve() / "port"), "-I", str(here),
        str(here / "tests" / "renderer_test.c"), str(here / "frame_bridge.c"),
        *(str(source) for source in upstream_sources(args.upstream)), "-lm",
        "-sENVIRONMENT=node", "-sSINGLE_FILE=1", "-sWASM_ASYNC_COMPILATION=0", "-sEXIT_RUNTIME=1",
        "-o", str(executable),
    ], check=True)
    subprocess.run([args.node, str(executable)], check=True)
