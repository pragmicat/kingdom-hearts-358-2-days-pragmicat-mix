#!/usr/bin/env python3
"""Compile seven patched Days menu units and check their desktop branches."""

from argparse import ArgumentParser
from pathlib import Path
import re
import subprocess
from tempfile import TemporaryDirectory

HERE = Path(__file__).resolve().parent
PINNED_UPSTREAM = "2037f79a171524eafcf40a1d641b16da14667a64"
MENU_UNITS = ("camp", "charselect", "cinelist", "daycard", "enemy_viewer", "newgame", "shop")


def run(args):
    head = subprocess.check_output(
        ["git", "-C", str(args.upstream), "rev-parse", "HEAD"], text=True
    ).strip()
    if head != PINNED_UPSTREAM:
        raise SystemExit(f"Use Days PC port source {PINNED_UPSTREAM}.")
    version = subprocess.check_output([args.emcc, "--version"], text=True).splitlines()[0]
    if not re.search(r"\b4\.0\.23\b", version):
        raise SystemExit(f"Use Emscripten 4.0.23, found {version}.")
    port = args.upstream / "port"
    with TemporaryDirectory(prefix="days-menu-wasm-") as temp:
        output = Path(temp)
        (output / "khdays_ds_symbols.h").write_text(
            "#define KHDAYS_DS_gSceneCtl 0x0204bda8u\n"
            "#define KHDAYS_DS_data_ov000_0205ac24 0x0205ac24u\n",
            encoding="utf-8",
        )
        flags = [
            "-std=c17", "-O2", "-Wall", "-Wextra", "-Werror", "-fdeclspec",
            "-DKHDAYS_BROWSER=1", "-I", str(port), "-I", str(output),
            "-I", str(args.upstream / "sdk/include"),
        ]
        windows = output / "windows-fixture"
        windows.mkdir()
        (windows / "windows.h").touch()
        for name in MENU_UNITS:
            relative = f"port/runtime/menus/{name}.c"
            source = args.upstream / relative
            subprocess.run(
                [args.emcc, *flags, "-c", str(source), "-o", str(output / f"{name}.o")],
                check=True,
            )
            original = output / f"{name}.c"
            original.write_bytes(subprocess.check_output(
                ["git", "-C", str(args.upstream), "show", f"{PINNED_UPSTREAM}:{relative}"]
            ))
            preprocessor = [args.emcc, "-E", "-P", "-D_WIN32", "-D_MSC_VER=1938",
                            "-I", str(windows), "-I", str(source.parent), "-I", str(output),
                            "-I", str(args.upstream / "sdk/include")]
            old = subprocess.check_output([*preprocessor, str(original)], text=True)
            new = subprocess.check_output([*preprocessor, str(source)], text=True)
            if old.split() != new.split():
                raise AssertionError(f"Windows preprocessing changed for {name}")

        wasm = output / "daycard_time.js"
        subprocess.run(
            [args.emcc, *flags, str(HERE / "tests/daycard_time.c"), "-sENVIRONMENT=node",
             "-sEXIT_RUNTIME=1", "-o", str(wasm)],
            check=True,
        )
        subprocess.run([args.node, str(wasm)], check=True)
    print("Seven native menu objects compile to Wasm, Windows tokens match, and day-card timing passed.")


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", type=Path, required=True)
    parser.add_argument("--emcc", default="emcc")
    parser.add_argument("--node", default="node")
    run(parser.parse_args())
