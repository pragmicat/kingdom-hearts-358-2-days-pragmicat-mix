#!/usr/bin/env python3
"""Reject changed upstream renderer/context dependencies before a local build."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

HERE = Path(__file__).resolve().parent
PIN = json.loads((HERE / "UPSTREAM.json").read_text(encoding="utf-8"))

def verify(upstream: Path) -> None:
    for relative, expected in PIN["sha256"].items():
        source = upstream.resolve() / relative
        if not source.is_file() or hashlib.sha256(source.read_bytes()).hexdigest() != expected:
            raise ValueError(f"Upstream dependency differs from khdays-port {PIN['tag']}: {relative}")

def verify_emcc(emcc: str) -> None:
    version = subprocess.run([emcc, "--version"], check=True, capture_output=True, text=True).stdout.splitlines()[0]
    if not re.search(r"\b" + re.escape(PIN["emscripten"]) + r"\b", version):
        raise ValueError(f"Use the tested Emscripten {PIN['emscripten']} toolchain; found: {version}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--upstream", required=True, type=Path)
    args = parser.parse_args()
    verify(args.upstream)
    print(f"Verified source dependencies from khdays-port {PIN['commit']}")
