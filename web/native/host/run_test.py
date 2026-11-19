#!/usr/bin/env python3
"""Compile real upstream host interfaces and execute ROM-free Wasm regressions."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
from verify_upstream import verify, verify_emcc


def verify_host(upstream: Path) -> None:
    verify(upstream)
    pin = json.loads((HERE / "UPSTREAM_HOST.json").read_text(encoding="utf-8"))
    for relative, digest in pin["sha256"].items():
        source = upstream.resolve() / relative
        if not source.is_file() or hashlib.sha256(source.read_bytes()).hexdigest() != digest:
            raise ValueError(f"Pinned upstream host interface differs: {relative}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--upstream", required=True, type=Path)
    parser.add_argument("--emcc", default="emcc")
    parser.add_argument("--node", default="node")
    args = parser.parse_args()
    verify_host(args.upstream)
    # The native controls comparison also executes one original public decomp
    # routine. Keep these optional-source pins separate from the host ABI so
    # unrelated audio-component validation does not require that submodule.
    analog_pin = json.loads((HERE / "UPSTREAM_ANALOG.json").read_text(encoding="utf-8"))
    for relative, digest in analog_pin["sha256"].items():
        source = args.upstream.resolve() / relative
        if not source.is_file() or hashlib.sha256(source.read_bytes()).hexdigest() != digest:
            raise ValueError(f"Pinned analog regression dependency differs or is missing: {relative}")
    verify_emcc(args.emcc)
    port = args.upstream.resolve() / "port"
    context = HERE.parent / "context" / "context_emscripten.c"
    with tempfile.TemporaryDirectory(prefix="days-host-wasm-") as directory:
        for optimization in ("-O1", "-O2"):
            executable = Path(directory) / "host-smoke.js"
            subprocess.run([
                args.emcc, "-std=c11", optimization, "-Wall", "-Wextra", "-Werror", "-fdeclspec",
                "-sASYNCIFY", "-sASSERTIONS=2", "-sSTACK_OVERFLOW_CHECK=2", "-sALLOW_MEMORY_GROWTH=1",
                "-I", str(port), "-I", str(port / "host/app"), "-I", str(port / "runtime/core"), "-I", str(HERE),
                str(HERE / "clock_emscripten.c"), str(HERE / "input_emscripten.c"),
                str(HERE / "instance_emscripten.c"), str(context), str(HERE / "tests/host_smoke.c"),
                str(port / "host/app/bindings.c"), str(port / "runtime/input/controls_hooks.c"),
                str(port / "runtime/input/analog.c"),
                str(args.upstream.resolve() / "external/khdays-decomp/src/overlays/field/ov022_battle/Ov022_HeadingFromHeldDpad.c"),
                "-o", str(executable),
            ], check=True)
            subprocess.run([args.node, str(executable)], check=True)
