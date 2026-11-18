#!/usr/bin/env python3
"""Compile upstream browser entry objects and run the frame host in Wasm."""

import argparse
from pathlib import Path
import re
import subprocess
from tempfile import TemporaryDirectory


PINNED_PORT = "2037f79a171524eafcf40a1d641b16da14667a64"
PINNED_SDL = "f87239e71e42da91ca317a12eefb82cfbf3393eb"
SOURCES = (
    "port/runtime/core/start.c",
    "port/runtime/core/runtime.c",
    "port/runtime/core/diag.c",
    "port/runtime/core/boot.c",
    "port/runtime/core/io_regs.c",
    "port/runtime/core/language.c",
    "port/runtime/display/display.c",
    "port/runtime/display/field_wipe.c",
    "port/runtime/display/gfx_check.c",
    "port/runtime/movies/video.c",
    "port/runtime/plugins/plugins.c",
)
HERE = Path(__file__).resolve().parent


def check_commit(repository, expected):
    actual = subprocess.check_output(
        ["git", "-C", str(repository), "rev-parse", "HEAD"], text=True
    ).strip()
    if actual != expected:
        raise SystemExit(f"{repository} must use commit {expected}")


def run(args):
    check_commit(args.upstream, PINNED_PORT)
    check_commit(args.sdl, PINNED_SDL)
    version = subprocess.check_output([args.emcc, "--version"], text=True).splitlines()[0]
    if not re.search(r"\b4\.0\.23\b", version):
        raise SystemExit(f"Expected Emscripten 4.0.23, got {version}")

    with TemporaryDirectory(prefix="days-browser-start-") as temp:
        out = Path(temp)
        mobiclip = args.upstream / "external/khdays-decomp/libs/mobiclip/video/portable"
        # Only addresses needed by the compile check. Full builds use the
        # header emitted by the pinned reference rewriter.
        (out / "khdays_data_area.h").write_text(
            "#define KHDAYS_DATA_AREA 0x09000000u\n"
            "#define KHDAYS_DATA_AREA_END 0x09010000u\n"
            "#define KHDAYS_DATA_khdays_cpsr 0x09000000u\n",
            encoding="utf-8",
        )
        (out / "khdays_ds_symbols.h").write_text(
            "#define KHDAYS_DS_gSceneCtl 0x02000000u\n"
            "#define KHDAYS_DS_gLangPath 0x02000004u\n"
            "#define KHDAYS_DS_data_0204c238 0x0204c238u\n",
            encoding="utf-8",
        )
        flags = [
            "-std=c17", "-O2", "-fdeclspec", "-Wall", "-Wextra", "-Werror",
            "-I", str(out), "-I", str(args.upstream / "sdk/include"),
            "-I", str(args.sdl / "include"), "-I", str(args.upstream / "port"),
            "-I", str(mobiclip),
        ]
        for source in SOURCES:
            subprocess.run(
                [args.emcc, *flags, "-DKHDAYS_BROWSER=1", "-c", str(args.upstream / source),
                 "-o", str(out / (Path(source).name + ".o"))],
                check=True,
            )
        for source in ("mobiclip_frame_core.cpp", "mobiclip_reference.cpp"):
            subprocess.run(
                [args.emcc, "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror",
                 "-Wno-unused-const-variable", "-c", str(mobiclip / source),
                 "-o", str(out / (source + ".o"))],
                check=True,
            )
        print("Actual upstream entry, core, display and plugin units compiled to Wasm.")

        # Preprocessor parity check only: real SDL headers are used above.
        # Empty platform headers allow comparing active Windows branches even
        # on a machine without the Windows SDK. Never use them to build.
        win_headers = out / "win-parity"
        (win_headers / "SDL3").mkdir(parents=True)
        for name in ("windows.h", "dbghelp.h", "direct.h", "SDL3/SDL.h"):
            (win_headers / name).touch()
        for source in SOURCES:
            current = args.upstream / source
            only_preprocess = [
                args.emcc, "-E", "-P", "-D_WIN32", "-D_M_X64",
                "-I", str(win_headers), "-I", str(out),
                "-I", str(args.upstream / "sdk/include"), "-I", str(current.parent), "-I", str(mobiclip),
            ]
            original = out / Path(source).name
            original.write_bytes(subprocess.check_output(
                ["git", "-C", str(args.upstream), "show", f"{PINNED_PORT}:{source}"]
            ))
            before = subprocess.check_output([*only_preprocess, str(original)], text=True)
            after = subprocess.check_output([*only_preprocess, str(current)], text=True)
            # Ignore blank lines created by additional conditional directives.
            if before.split() != after.split():
                raise AssertionError(f"Windows preprocessing changed: {source}")
        print("Windows preprocessor tokens match the pinned upstream.")

        module = out / "display.mjs"
        gpu = args.upstream / "port/gpu"
        gpu_objects = []
        for name in ("gpu2d.c", "gpu3d.c", "texdecode.c", "vram_map.c"):
            obj = out / (name + ".o")
            subprocess.run(
                [args.emcc, *flags, "-Wno-unused-function", "-c", str(gpu / name), "-o", str(obj)],
                check=True,
            )
            gpu_objects.append(str(obj))
        subprocess.run(
            [args.emcc, *flags, "-I", str(args.renderer),
             str(HERE / "host_display_emscripten.c"), str(HERE / "tests/display_smoke.c"),
             str(args.renderer / "frame_bridge.c"),
             *gpu_objects,
             "-lm", "--no-entry", "-sMODULARIZE=1", "-sEXPORT_ES6=1", "-sENVIRONMENT=node",
             "-sASSERTIONS=2", "-sEXPORTED_RUNTIME_METHODS=['HEAPU8']", "-o", str(module)],
            check=True,
        )
        subprocess.run([args.node, str(HERE / "tests/display_smoke.mjs"), str(module)], check=True)

        diagnostic = out / "diag.js"
        subprocess.run(
            [args.emcc, *flags, str(out / "diag.c.o"), str(HERE / "tests/diag_smoke.c"),
             "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1", "-o", str(diagnostic)],
            check=True,
        )
        subprocess.run([args.node, str(diagnostic)], check=True)
        invalid = subprocess.run(
            [args.node, str(diagnostic), "invalid-next"], capture_output=True, text=True
        )
        if invalid.returncode != 12 or "no function's reference" not in invalid.stderr:
            raise AssertionError("The original invalid update was not rejected.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", type=Path, required=True)
    parser.add_argument("--sdl", type=Path, required=True)
    parser.add_argument("--renderer", type=Path, default=HERE.parent / "renderer")
    parser.add_argument("--emcc", default="emcc")
    parser.add_argument("--node", default="node")
    run(parser.parse_args())
