#!/usr/bin/env python3
"""Compile the pinned Days network sources and exercise offline startup in Wasm."""

from argparse import ArgumentParser
from pathlib import Path
import re
import subprocess
from tempfile import TemporaryDirectory

HERE = Path(__file__).resolve().parent
PINNED_UPSTREAM = "2037f79a171524eafcf40a1d641b16da14667a64"
SOURCES = (
    "runtime/net/net_session.c",
    "runtime/net/roomlist.c",
    "runtime/net/mp_predict.c",
    "runtime/net/msg_overflow.c",
    "host/net/net_browser.c",
)


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
    sdk = args.upstream / "sdk/include"
    flags = [
        "-std=c17", "-O2", "-Wall", "-Wextra", "-Werror", "-Wno-unknown-pragmas",
        "-fdeclspec", "-DKHDAYS_BROWSER=1", "-DKHDAYS_BROWSER_MEMORY=1",
        "-I", str(port), "-I", str(sdk), "-I", str(args.sdl_include),
    ]
    with TemporaryDirectory(prefix="days-offline-net-") as temp:
        output = Path(temp)
        for name in SOURCES:
            source = port / name
            subprocess.run(
                [args.emcc, *flags, "-c", str(source), "-o", str(output / f"{source.stem}.o")],
                check=True,
            )

        roomlist = port / "runtime/net/roomlist.c"
        original = output / "roomlist.c"
        original.write_bytes(subprocess.check_output(
            ["git", "-C", str(args.upstream), "show", f"{PINNED_UPSTREAM}:port/runtime/net/roomlist.c"]
        ))
        # No Windows SDK is needed for comparing the source tokens here.
        (output / "windows.h").touch()
        preprocess = [args.emcc, "-E", "-P", "-D_WIN32", "-D_MSC_VER=1938", "-fdeclspec",
                      "-I", str(output), "-I", str(roomlist.parent), "-I", str(sdk)]
        before = subprocess.check_output([*preprocess, str(original)], text=True)
        after = subprocess.check_output([*preprocess, str(roomlist)], text=True)
        if before.split() != after.split():
            raise AssertionError("Windows roomlist preprocessing changed")

        script = output / "offline_net.js"
        subprocess.run(
            [args.emcc, *flags, str(HERE / "tests/offline_net.c"), str(output / "net_session.o"),
             str(output / "net_browser.o"), str(port / "runtime/core/session_log.c"),
             str(port / "runtime/core/sha1.c"), str(args.sdl_wasm_archive), "-sENVIRONMENT=node",
             "-sEXIT_RUNTIME=1", "-o", str(script)],
            check=True,
        )
        subprocess.run([args.node, str(script)], check=True)
    print("Five native network objects compile to Wasm and Windows roomlist tokens match.")


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", type=Path, required=True)
    parser.add_argument("--sdl-include", type=Path, required=True)
    parser.add_argument("--sdl-wasm-archive", type=Path, required=True)
    parser.add_argument("--emcc", default="emcc")
    parser.add_argument("--node", default="node")
    run(parser.parse_args())
