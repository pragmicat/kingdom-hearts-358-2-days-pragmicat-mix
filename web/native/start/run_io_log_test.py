#!/usr/bin/env python3
"""Compile the real browser card-read accounting and session log and execute Wasm."""

import argparse
from os import environ
from pathlib import Path
import re
import subprocess
from tempfile import TemporaryDirectory

from run_test import HERE, PINNED_PORT, check_commit


def main(args):
    check_commit(args.upstream, PINNED_PORT)
    version = subprocess.check_output([args.emcc, "--version"], text=True).splitlines()[0]
    if not re.search(r"\b4\.0\.23\b", version):
        raise SystemExit(f"Expected Emscripten 4.0.23, got {version}")

    port = args.upstream / "port"
    flags = ["-std=c17", "-O2", "-Wall", "-Wextra", "-Werror", "-fdeclspec",
             "-DKHDAYS_BROWSER=1", "-I", str(port)]
    with TemporaryDirectory(prefix="days-browser-io-log-") as temp:
        out = Path(temp)
        # Compile the real event scheduler as well, including its stall report.
        subprocess.run([args.emcc, *flags, "-c", str(port / "runtime/core/events.c"),
                        "-o", str(out / "events.o")], check=True)
        binary = out / "io_log.js"
        subprocess.run(
            [args.emcc, *flags, str(port / "runtime/core/files.c"),
             str(port / "runtime/core/session_log.c"), str(HERE / "tests/io_log_smoke.c"),
             "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1", "-o", str(binary)],
            check=True,
        )
        env = dict(environ)
        env.pop("KHDAYS_FILES", None)
        env.pop("KHDAYS_LOG", None)
        command = [args.node, str(binary)]
        result = subprocess.run(command, capture_output=True, text=True, env=env, check=True)
        log = result.stderr
        assert log.count("read fixture.bin from +0x2: 4 bytes in 2 reads") == 1
        assert log.count("read (the image's 0x00008141) from +0x0: 512 bytes in 1 reads") == 1
        assert log.count("files: 4 reads, 516 bytes") == 1
        assert "card: 2 commands, 1024 bytes" in log
        assert "f17     mark 1 (F12)" in log
        assert "marker after disabled" not in log
        for mode in ("card", "check"):
            subprocess.run([*command, mode], env=env, check=True)
        limited = subprocess.run([*command, "limit"], capture_output=True, env=env, check=True)
        assert limited.stderr.count(b"session log limit reached") == 1
        assert len(limited.stderr) <= 2 * 1024 * 1024 + 64
    print("Wasm native ROM reads, card remapping, grouped accounting and bounded session log passed.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", type=Path, required=True)
    parser.add_argument("--emcc", default="emcc")
    parser.add_argument("--node", default="node")
    main(parser.parse_args())
