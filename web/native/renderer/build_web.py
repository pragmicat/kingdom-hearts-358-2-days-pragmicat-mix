#!/usr/bin/env python3
"""Build the upstream CPU renderer adapter with Emscripten, without CMake."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from verify_upstream import verify, verify_emcc


def upstream_sources(upstream: Path) -> list[Path]:
    verify(upstream)
    port = upstream.resolve() / "port"
    sources = [port / "gpu" / name for name in ("gpu2d.c", "gpu3d.c", "texdecode.c", "vram_map.c")]
    if not all(source.is_file() for source in sources):
        raise ValueError("--upstream must name a khdays-port v0.1.5 source checkout")
    return sources


def build(emcc: str, upstream: Path, output: Path) -> Path:
    verify_emcc(emcc)
    here = Path(__file__).resolve().parent
    sources = upstream_sources(upstream)
    output.mkdir(parents=True, exist_ok=True)
    module = output.resolve() / "days_software_renderer.mjs"
    subprocess.run([
        emcc, "-std=c17", "-O2", "-I", str(upstream.resolve() / "port"),
        str(here / "frame_bridge.c"), *(str(source) for source in sources), "-lm", "--no-entry",
        "-sMODULARIZE=1", "-sEXPORT_ES6=1", "-sENVIRONMENT=web,worker,node", "-sALLOW_MEMORY_GROWTH=1",
        "-sFILESYSTEM=0", "-sEXPORTED_RUNTIME_METHODS=['HEAPU8']",
        "-sEXPORTED_FUNCTIONS=['_malloc','_free','_days_web_frame_rgba','_days_web_frame_generation',"
        "'_days_web_present','_days_web_render','_days_web_reset_renderer']", "-o", str(module),
    ], check=True)
    # Carry required notices with the redistributable renderer binaries.
    shutil.copyfile(upstream.resolve() / "LICENSE", output / "UPSTREAM_LICENSE.txt")
    compiler_root = Path(shutil.which(emcc) or emcc).resolve().parent
    for relative, filename in [
        ("LICENSE", "EMSCRIPTEN_LICENSE.txt"),
        ("system/lib/libc/musl/COPYRIGHT", "MUSL_COPYRIGHT.txt"),
    ]:
        notice = compiler_root / relative
        if not notice.is_file():
            raise FileNotFoundError(f"Cannot package the renderer runtime notice: {notice}")
        shutil.copyfile(notice, output / filename)
    manifest = {
        "component": "khdays-port v0.1.5 CPU software renderer with browser adapter",
        "game_runtime": False,
        "upstream": "https://github.com/Yokimitsuro/khdays-port/tree/v0.1.5",
        "sources": {str(source.relative_to(upstream.resolve())): hashlib.sha256(source.read_bytes()).hexdigest()
                    for source in sources},
        "outputs": {filename: hashlib.sha256((output / filename).read_bytes()).hexdigest()
                    for filename in ("days_software_renderer.mjs", "days_software_renderer.wasm")},
    }
    (output / "renderer-provenance.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return module


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--upstream", type=Path, required=True)
    parser.add_argument("--emcc", default="emcc")
    parser.add_argument("--output", type=Path, default=Path("build-web"))
    args = parser.parse_args()
    print(build(args.emcc, args.upstream, args.output))
