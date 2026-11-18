#!/usr/bin/env python3
"""ROM-free actual upstream DS mixer to browser host PCM forwarding in Wasm."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
from verify_upstream import verify_emcc
from run_test import verify_host


def verify_audio(upstream: Path) -> None:
    verify_host(upstream)
    pin = json.loads((HERE / "UPSTREAM_AUDIO.json").read_text(encoding="utf-8"))
    for relative, digest in pin["sha256"].items():
        source = upstream.resolve() / relative
        if not source.is_file() or hashlib.sha256(source.read_bytes()).hexdigest() != digest:
            raise ValueError(f"Pinned upstream audio interface/source differs: {relative}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--upstream", required=True, type=Path)
    parser.add_argument("--emcc", default="emcc")
    parser.add_argument("--node", default="node")
    args = parser.parse_args()
    verify_audio(args.upstream)
    verify_emcc(args.emcc)
    port = args.upstream.resolve() / "port"
    with tempfile.TemporaryDirectory(prefix="days-audio-wasm-") as directory:
        module = Path(directory) / "audio-smoke.mjs"
        exports = ["_malloc", "_free", "_days_web_audio_set_device_rate", "_days_web_audio_stop",
                   "_days_test_audio_start", "_days_test_audio_advances", "_days_test_audio_cycle",
                   "_days_test_audio_exact", "_days_test_emit_mixer", "_days_test_emit_float",
                   "_days_test_audio_pause", "_days_test_audio_mute", "_days_test_audio_invalid", "_days_test_audio_fiber", "_days_test_audio_fiber_completed"]
        subprocess.run([
            args.emcc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-fdeclspec",
            "-sASYNCIFY", "-sASSERTIONS=2", "-sSTACK_OVERFLOW_CHECK=2", "-sALLOW_MEMORY_GROWTH=1",
            "-sMODULARIZE=1", "-sEXPORT_ES6=1", "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1", "--no-entry",
            "-sEXPORTED_RUNTIME_METHODS=['HEAPU8','ccall']", "-sEXPORTED_FUNCTIONS=" + json.dumps(exports),
            "-I", str(port / "host/app"), "-I", str(port / "runtime/core"), "-I", str(port), "-I", str(HERE),
            str(HERE / "clock_emscripten.c"), str(HERE / "audio_emscripten.c"),
            str(HERE.parent / "context/context_emscripten.c"),
            str(port / "sound/channels.c"), str(port / "sound/mix_ds.c"), str(HERE / "tests/audio_smoke.c"),
            "-o", str(module),
        ], check=True)
        subprocess.run([args.node, str(HERE / "tests/wasm-audio.test.mjs"), str(module)], check=True, timeout=30)
