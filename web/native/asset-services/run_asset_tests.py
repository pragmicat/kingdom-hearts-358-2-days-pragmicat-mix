"""Build the browser texture/asset paths and run ROM-free Wasm smoke tests."""
import argparse
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
PINNED_UPSTREAM = "2037f79a171524eafcf40a1d641b16da14667a64"
PINNED_STB = "2c980bb59875b0d32144a71867fbdebb2f77cd20"

def run(args):
    upstream, stb = args.upstream.resolve(), args.stb.resolve()
    subprocess.run(["git", "-C", str(upstream), "merge-base", "--is-ancestor",
                    PINNED_UPSTREAM, "HEAD"], check=True)
    stb_commit = subprocess.check_output(["git", "-C", str(stb), "rev-parse", "HEAD"], text=True).strip()
    if stb_commit != PINNED_STB:
        raise SystemExit(f"Use the upstream stb pin {PINNED_STB}")
    emcc = Path(shutil.which(args.emcc) or args.emcc).resolve()
    emxx = emcc.with_name("em++")
    version = subprocess.check_output([str(emcc), "--version"], text=True).splitlines()[0]
    if not re.search(r"\b4\.0\.23\b", version):
        raise SystemExit(f"Use Emscripten 4.0.23, found {version}")
    port, assets = upstream / "port", upstream / "assets"
    paths = ["-I", str(port), "-I", str(assets / "include"), "-I", str(stb),
             "-I", str(args.sdl_include.resolve())]
    link = ["-sASSERTIONS=2", "-sSTACK_OVERFLOW_CHECK=2", "-sALLOW_MEMORY_GROWTH=1",
            "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1"]
    with tempfile.TemporaryDirectory(prefix="days-wasm-assets-") as work:
        output = Path(work)
        sources = (
            "runtime/mods/textures.c", "runtime/mods/sheet_colours.c",
            "host/render/png_io.c", "gpu/texdecode.c",
        )

        def compile_c(relative):
            obj = output / (Path(relative).stem + ".o")
            subprocess.run([str(emcc), "-std=c17", "-O2", "-fms-extensions", "-fsigned-char",
                            "-DKHDAYS_BROWSER=1", "-DKHDAYS_BROWSER_MEMORY=1", *paths,
                            "-c", str(port / relative), "-o", str(obj)], check=True)
            return obj

        with ThreadPoolExecutor(max_workers=4) as workers:
            objects = list(workers.map(compile_c, sources))
        # The test invokes texture I/O. The palette collection file builds
        # separately and retains its actual display/ROM dependencies.
        offline = output / "texture-assets.js"
        subprocess.run([
            str(emxx), "-std=c++20", "-O2", "-fexceptions", "-fms-extensions",
            *paths, str(HERE / "tests/asset_loading.cpp"), str(port / "runtime/core/rom.cpp"),
            str(assets / "src/filesystem.cpp"), str(objects[0]), str(objects[2]), str(objects[3]),
            str(args.sdl_archive.resolve()), *link, "-o", str(offline),
        ], check=True)
        subprocess.run([args.node, str(offline)], cwd=output, check=True)
        fixtures = {
            "vfs": ["filesystem.cpp"],
            "message": ["message.cpp"],
            "font": ["font.cpp", "message.cpp"],
            "graphics2d": ["graphics2d.cpp", "message.cpp"],
        }

        def check_fixture(case):
            name, units = case
            target = output / f"{name}.js"
            subprocess.run([str(emxx), "-std=c++20", "-O2", "-fexceptions", *paths,
                            str(assets / f"tests/test_{name}.cpp"),
                            *(str(assets / "src" / unit) for unit in units),
                            *link, "-o", str(target)], check=True)
            subprocess.run([args.node, str(target)], cwd=output, check=True)

        with ThreadPoolExecutor(max_workers=4) as workers:
            list(workers.map(check_fixture, fixtures.items()))

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", type=Path, required=True)
    parser.add_argument("--stb", type=Path, required=True)
    parser.add_argument("--sdl-include", type=Path, required=True)
    parser.add_argument("--sdl-archive", type=Path, required=True)
    parser.add_argument("--emcc", required=True)
    parser.add_argument("--node", default="node")
    run(parser.parse_args())
