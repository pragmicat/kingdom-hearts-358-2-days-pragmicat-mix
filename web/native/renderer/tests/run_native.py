#!/usr/bin/env python3
"""Compile the actual upstream CPU renderer and run the bridge regression."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from verify_upstream import verify

parser = argparse.ArgumentParser()
parser.add_argument("--upstream", type=Path, required=True)
args = parser.parse_args()
verify(args.upstream)
here = Path(__file__).resolve().parent.parent
port = args.upstream.resolve() / "port"
sources = [port / "gpu" / filename for filename in ("gpu2d.c", "gpu3d.c", "texdecode.c", "vram_map.c")]
with tempfile.TemporaryDirectory(prefix="days-cpu-renderer-") as work:
    binary = Path(work) / "renderer-test"
    compiler = shlex.split(os.environ.get("CC", "cc"))
    common = ["-std=c17", "-O2", "-Wall", "-Wextra", "-I", str(port), "-I", str(here)]
    objects = []
    for source in [here / "frame_bridge.c", here / "tests" / "renderer_test.c", *sources]:
        obj = Path(work) / (source.stem + ".o")
        # The pinned upstream gpu3d.c retains compare_translucent, an unused
        # static helper. Keep its source intact; adapter/tests remain strict.
        warnings = ["-Werror"] if source not in sources else ["-Wno-unused-function", "-Werror"]
        subprocess.run([*compiler, *common, *warnings, "-c", str(source), "-o", str(obj)], check=True)
        objects.append(str(obj))
    subprocess.run([*compiler, *objects, "-lm", "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
