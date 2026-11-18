"""Build and run the patched Days runtime memory and fiber adapters without a ROM."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
PIN = "2037f79a171524eafcf40a1d641b16da14667a64"
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--upstream", type=Path, required=True)
parser.add_argument("--emcc", default="emcc")
parser.add_argument("--node", default="node")
parser.add_argument("--context", type=Path, required=True)
args = parser.parse_args()
upstream = args.upstream.resolve()
ancestor = subprocess.run(["git", "merge-base", "--is-ancestor", PIN, "HEAD"], cwd=upstream)
if ancestor.returncode != 0:
    raise SystemExit(f"Use a checkout based on khdays-port v0.1.5 ({PIN})")
version = subprocess.check_output([args.emcc, "--version"], text=True).splitlines()[0]
if not re.search(r"\b4\.0\.23\b", version):
    raise SystemExit(f"Use Emscripten 4.0.23 instead of {version}")
with tempfile.TemporaryDirectory(prefix="days-runtime-memory-") as directory:
    build = Path(directory)
    (build / "khdays_data_area.h").write_text(
        "#define KHDAYS_IO_VIEW 0x0b100000u\n"
        "#define KHDAYS_IO2_VIEW 0x0b110000u\n"
        "#define KHDAYS_DATA_AREA 0x0b200000u\n"
        "#define KHDAYS_DATA_AREA_END 0x0b200100u\n",
        encoding="utf-8",
    )
    # game_link.c needs generated declarations even though the used memory
    # functions do not depend on the addresses of these game objects.
    (build / "khdays_ds_symbols.h").write_text(
        "#define KHDAYS_DS_gObjSystem 0x02000000u\n"
        "#define KHDAYS_DS_gPauseMode 0x02000000u\n"
        "#define KHDAYS_DS_gSceneCtl 0x02000000u\n"
        "#define KHDAYS_DS_gGameState 0x02000000u\n"
        "#define KHDAYS_DS_gMissionNumber 0x02000000u\n",
        encoding="utf-8",
    )
    output = build / "memory-test.js"
    port = upstream / "port"
    subprocess.run([
        args.emcc, "-std=c17", "-O2", "-Wall", "-Wextra", "-Werror",
        "-fdeclspec", "-Wno-unknown-pragmas", "-Wno-ignored-attributes",
        "-DKHDAYS_BROWSER_MEMORY=1",
        "-I", str(port), "-I", str(upstream / "sdk/include"),
        "-I", str(port / "host/app"), "-I", str(build),
        str(port / "runtime/core/memory.c"), str(port / "runtime/core/window.c"),
        str(port / "runtime/core/game_heap.c"), str(port / "runtime/plugins/engine.c"),
        str(port / "runtime/scripts/game_link.c"), str(args.context.resolve()), str(HERE / "tests/runtime_memory.c"),
        str(HERE / "tests/generated_pointer.c"),
        "-sASYNCIFY", "-sASSERTIONS=2", "-sSTACK_OVERFLOW_CHECK=2",
        "-sALLOW_MEMORY_GROWTH=1", "-o", str(output),
    ], check=True)
    subprocess.run([args.node, str(output)], check=True)
