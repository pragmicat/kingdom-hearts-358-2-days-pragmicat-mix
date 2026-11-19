#!/usr/bin/env python3
"""Compile real Days save units and exercise the browser's save path."""

from argparse import ArgumentParser
from pathlib import Path
import re
import subprocess
from tempfile import TemporaryDirectory

HERE = Path(__file__).resolve().parent
UPSTREAM = "2037f79a171524eafcf40a1d641b16da14667a64"


def run(args):
    head = subprocess.check_output(
        ["git", "-C", str(args.upstream), "rev-parse", "HEAD"], text=True
    ).strip()
    if head != UPSTREAM:
        raise SystemExit(f"Use pinned Days port source {UPSTREAM}.")
    version = subprocess.check_output([args.emcc, "--version"], text=True).splitlines()[0]
    if not re.search(r"\b4\.0\.23\b", version):
        raise SystemExit(f"Use Emscripten 4.0.23, found {version}.")

    port = args.upstream / "port"
    backend = args.mod / "web/native/mod"
    tests = HERE / "tests"
    with TemporaryDirectory(prefix="days-save-wasm-") as temp:
        output = Path(temp)
        # Only the build test uses this short generated-header fixture.
        # The real runtime consumes the full upstream generator output.
        (output / "khdays_ds_symbols.h").write_text(
            "#define KHDAYS_DS_gSceneCtl 0x0204bda8u\n"
            "#define KHDAYS_DS_data_ov000_0205ac24 0x0205ac24u\n",
            encoding="utf-8",
        )
        flags = [
            "-std=c17", "-O2", "-Wall", "-Wextra", "-Werror", "-fdeclspec",
            "-DKHDAYS_BROWSER=1", "-I", str(port), "-I", str(output),
        ]
        for name in ("autosave", "saveworld", "savelist", "saveslots"):
            subprocess.run(
                [args.emcc, *flags, "-c", str(port / f"runtime/menus/{name}.c"),
                 "-o", str(output / f"{name}.o")],
                check=True,
            )
        mode = output / "autosave_mode.js"
        subprocess.run([args.emcc, *flags, str(tests / "autosave_mode.c"),
                        "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1", "-o", str(mode)], check=True)
        for setting in ("campaign", "practice", "explicit"):
            subprocess.run([args.node, str(mode), setting], check=True)

        slots = output / "saveslots.mjs"
        subprocess.run(
            [args.emcc, *flags, "-I", str(tests), str(port / "tests/saveslots_test.c"),
             str(output / "saveslots.o"), str(port / "runtime/core/sha1.c"),
             str(backend / "files_emscripten.c"), str(backend / "profile_mode.c"),
             str(tests / "save_slots_entry.c"), "-sASYNCIFY", "-sINVOKE_RUN=0",
             "-sFORCE_FILESYSTEM=1", "-sMODULARIZE=1", "-sEXPORT_ES6=1",
             "-sENVIRONMENT=node", "-lidbfs.js", "-sEXPORTED_RUNTIME_METHODS=['FS','ccall']",
             "-o", str(slots)],
            check=True,
        )
        subprocess.run([args.node, str(tests / "save_slots.mjs"), str(slots), str(args.mod)], check=True)
    print("Four native save units compiled and the WebAssembly save-slot/profile tests passed.")


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", type=Path, required=True)
    parser.add_argument("--mod", type=Path, required=True)
    parser.add_argument("--emcc", default="emcc")
    parser.add_argument("--node", default="node")
    run(parser.parse_args())
