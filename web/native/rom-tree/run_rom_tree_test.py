"""Run the native Days cartridge reader against a synthetic browser extraction."""
import argparse
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
UPSTREAM = "2037f79a171524eafcf40a1d641b16da14667a64"
STB = "2c980bb59875b0d32144a71867fbdebb2f77cd20"


def run(args):
    upstream = args.upstream.resolve()
    subprocess.run(["git", "-C", str(upstream), "merge-base", "--is-ancestor", UPSTREAM, "HEAD"],
                   check=True)
    stb = args.stb.resolve()
    if subprocess.check_output(["git", "-C", str(stb), "rev-parse", "HEAD"], text=True).strip() != STB:
        raise SystemExit("Use the Days port's pinned stb checkout")
    emcc = Path(shutil.which(args.emcc) or args.emcc).resolve()
    emxx = emcc.with_name("em++")
    port, assets = upstream / "port", upstream / "assets"
    includes = ["-I", str(port), "-I", str(assets / "include"), "-I", str(stb),
                "-I", str(args.sdl_include.resolve())]
    c_src = [port / name for name in (
        "runtime/mods/list.c", "runtime/mods/days.c", "runtime/mods/zip_read.c",
        "runtime/scripts/manifest.c", "runtime/scripts/order.c", "runtime/core/sha256.c",
        "host/render/png_io.c",
    )]
    cpp_src = [
        port / "runtime/core/rom.cpp", port / "runtime/core/text_patches.cpp",
        port / "runtime/scripts/folders.cpp", assets / "src/filesystem.cpp",
        assets / "src/message.cpp", HERE / "rom_tree_test.cpp",
    ]
    with tempfile.TemporaryDirectory(prefix="days-wasm-cart-tree-") as work:
        build = Path(work)

        def compile_unit(job):
            index, source = job
            output = build / f"{index}.o"
            is_cpp = source.suffix == ".cpp"
            compiler = emxx if is_cpp else emcc
            language = ["-std=c++20", "-fexceptions"] if is_cpp else ["-std=c17"]
            subprocess.run([str(compiler), *language, "-O2", "-fms-extensions",
                            *includes, "-c", str(source), "-o", str(output)], check=True)
            return output

        with ThreadPoolExecutor(max_workers=5) as workers:
            objects = list(workers.map(compile_unit, enumerate(c_src + cpp_src)))
        target = build / "cart-tree.mjs"
        subprocess.run([
            str(emxx), "-O2", "-fexceptions", "-sFORCE_FILESYSTEM=1", "-sINVOKE_RUN=0",
            "-sMODULARIZE=1", "-sEXPORT_ES6=1", "-sENVIRONMENT=node",
            "-sEXPORTED_RUNTIME_METHODS=FS,callMain", "-sASSERTIONS=2",
            "-sSTACK_OVERFLOW_CHECK=2", "-sALLOW_MEMORY_GROWTH=1",
            *map(str, objects), str(args.sdl_archive.resolve()), "-o", str(target),
        ], check=True)
        subprocess.run([
            args.node, str(HERE / "rom-tree.test.mjs"), str(target), str(args.importer.resolve()),
        ], cwd=build, check=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", required=True, type=Path)
    parser.add_argument("--importer", required=True, type=Path)
    parser.add_argument("--stb", required=True, type=Path)
    parser.add_argument("--sdl-include", required=True, type=Path)
    parser.add_argument("--sdl-archive", required=True, type=Path)
    parser.add_argument("--emcc", required=True)
    parser.add_argument("--node", default="node")
    run(parser.parse_args())
