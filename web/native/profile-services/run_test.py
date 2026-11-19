#!/usr/bin/env python3
"""Link the native profile services with SDL3 and check selected local settings."""

from argparse import ArgumentParser
from pathlib import Path
import re
import subprocess
from tempfile import TemporaryDirectory

HERE = Path(__file__).resolve().parent
PINNED_PORT = "2037f79a171524eafcf40a1d641b16da14667a64"
PINNED_SDL = "f87239e71e42da91ca317a12eefb82cfbf3393eb"
SOURCES = ("host/app/profile.c", "host/app/hud_settings.c", "host/screens/ui_text.c")


def check_commit(path, want):
    actual = subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()
    if actual != want:
        raise SystemExit(f"{path} must use commit {want}")


def run(args):
    check_commit(args.upstream, PINNED_PORT)
    check_commit(args.sdl, PINNED_SDL)
    version = subprocess.check_output([args.emcc, "--version"], text=True).splitlines()[0]
    if not re.search(r"\b4\.0\.23\b", version):
        raise SystemExit(f"Use Emscripten 4.0.23, found {version}.")
    port = args.upstream / "port"
    flags = [
        "-std=c17", "-O2", "-Wall", "-Wextra", "-Werror", "-fdeclspec", "-DKHDAYS_BROWSER=1",
        "-I", str(port), "-I", str(port / "host/app"), "-I", str(args.sdl / "include"),
    ]
    with TemporaryDirectory(prefix="days-browser-profile-") as temp:
        output = Path(temp)
        objects = []
        for relative in SOURCES:
            source = port / relative
            obj = output / f"{source.stem}.o"
            subprocess.run([args.emcc, *flags, "-c", str(source), "-o", str(obj)], check=True)
            objects.append(str(obj))
        script = output / "local_profile.js"
        subprocess.run(
            [args.emcc, *flags, *objects, str(HERE / "tests/local_profile.c"), str(HERE / "profile_start.c"),
             str(args.browser_native / "host/instance_emscripten.c"),
             str(args.browser_native / "mod/profile_mode.c"), str(args.sdl_wasm_archive),
             "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1", "-o", str(script)],
            check=True,
        )
        for mode in ("campaign", "practice"):
            subprocess.run([args.node, str(script), mode], check=True)


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", type=Path, required=True)
    parser.add_argument("--sdl", type=Path, required=True)
    parser.add_argument("--sdl-wasm-archive", type=Path, required=True)
    parser.add_argument("--browser-native", type=Path, required=True)
    parser.add_argument("--emcc", default="emcc")
    parser.add_argument("--node", default="node")
    run(parser.parse_args())
