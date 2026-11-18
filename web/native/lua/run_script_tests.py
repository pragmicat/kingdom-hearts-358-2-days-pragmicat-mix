"""Execute the upstream Days script tests using Lua 5.4.9 in WebAssembly."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
from pathlib import Path
import re
import subprocess
import tarfile
import tempfile

PIN = "2037f79a171524eafcf40a1d641b16da14667a64"
LUA_SHA256 = "2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6"
HERE = Path(__file__).resolve().parent
EXCLUDED_LUA = {"lua.c", "luac.c", "liolib.c", "ldblib.c", "loadlib.c", "linit.c"}

def run(args):
    upstream = args.upstream.resolve()
    subprocess.run(["git", "-C", str(upstream), "merge-base", "--is-ancestor", PIN, "HEAD"], check=True)
    version = subprocess.check_output([args.emcc, "--version"], text=True).splitlines()[0]
    if not re.search(r"\b4\.0\.23\b", version):
        raise SystemExit(f"Use Emscripten 4.0.23, found {version}")
    if hashlib.sha256(args.lua_archive.read_bytes()).hexdigest() != LUA_SHA256:
        raise SystemExit("Lua archive does not match the upstream 5.4.9 pin")
    port = upstream / "port"
    cmake = (port / "CMakeLists.txt").read_text(encoding="utf-8")
    case = re.search(r"add_executable\(khdays-script-test ([^)]+)\)", cmake)
    if case is None:
        raise SystemExit("Upstream script test source list is missing")
    script_sources = [port / item for item in case.group(1).split()]
    embed = []
    if args.practice is not None:
        script_sources[0] = HERE / "practice_loader_test.c"
        for local, virtual in (
            ("mod.ini", "/fixture/practice.ini"),
            ("scripts/main.lua", "/fixture/practice.lua"),
        ):
            source = args.practice.resolve() / local
            if not source.is_file():
                raise SystemExit(f"The Pragmicat practice script or manifest is missing: {source}")
            embed += ["--embed-file", f"{source}@{virtual}"]
    emcc = Path(args.emcc).resolve()
    emxx = emcc.with_name("em++")
    with tempfile.TemporaryDirectory(prefix="days-wasm-script-") as work:
        build = Path(work)
        with tarfile.open(args.lua_archive, "r:gz") as archive:
            archive.extractall(build, filter="data")
        lua = build / "lua-5.4.9" / "src"
        lua_sources = [src for src in sorted(lua.glob("*.c")) if src.name not in EXCLUDED_LUA]
        sources = lua_sources + script_sources
        opts = ["-O2", "-fms-extensions", "-fsigned-char", "-I", str(lua),
                "-I", str(args.sdl_include.resolve()), "-I", str(upstream / "sdk/include"),
                "-I", str(port)]
        def compile_unit(job):
            index, src = job
            obj = build / f"{index}.o"
            cxx = src.suffix == ".cpp"
            compiler = emxx if cxx else emcc
            lang = ["-std=c++20", "-fexceptions"] if cxx else ["-std=c17"]
            subprocess.run([str(compiler), *lang, *opts, "-c", str(src), "-o", str(obj)], check=True)
            return obj

        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            objects = list(pool.map(compile_unit, enumerate(sources)))
        output = build / "script-test.js"
        subprocess.run([
            str(emxx), "-O2", "-fexceptions", "-sASYNCIFY", "-sASSERTIONS=2",
            "-sSTACK_OVERFLOW_CHECK=2", "-sALLOW_MEMORY_GROWTH=1",
            "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1",
            *map(str, objects), str(args.sdl_archive.resolve()), *embed, "-o", str(output),
        ], check=True)
        subprocess.run([args.node, str(output)], cwd=build, check=True)
        print(f"Compiled {len(lua_sources)} Lua and {len(script_sources)} script test units for Wasm")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", required=True, type=Path)
    parser.add_argument("--lua-archive", required=True, type=Path)
    parser.add_argument("--sdl-include", required=True, type=Path)
    parser.add_argument("--sdl-archive", required=True, type=Path)
    parser.add_argument("--emcc", required=True)
    parser.add_argument("--practice", type=Path, help="Pragmicat's authored practice/ directory")
    parser.add_argument("--node", default="node")
    parser.add_argument("--jobs", type=int, default=6)
    run(parser.parse_args())
