#!/usr/bin/env python3
"""Build the actual patched runtime units with authored metadata as Wasm."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent.parent

parser = argparse.ArgumentParser()
parser.add_argument("--upstream", type=Path, required=True)
parser.add_argument("--patched", type=Path, required=True, help="upstream checkout with the source patches applied")
parser.add_argument("--emcc")
parser.add_argument("--cc", default="cc")
parser.add_argument("--node", default="node")
parser.add_argument("--memory", action="store_true", help="also execute actual browser memory.c with both browser macros")
args = parser.parse_args()
if args.memory and not args.emcc:
    parser.error("--memory requires --emcc for the actual Wasm integration check")
upstream = args.upstream.resolve()
patched = args.patched.resolve() / "port"
if not (patched / "tools/static_registry.py").is_file():
    parser.error("--patched must contain the applied static-registry source patch")
sys.path.insert(0, str(patched / "tools"))
from static_registry import write_registry
unit_env = dict(os.environ, KHDAYS_STATIC_REGISTRY_TOOLS=str(patched / "tools"))
subprocess.run([sys.executable, "-m", "unittest", "discover", "-s", str(HERE / "tests"), "-p", "test_*.py"], check=True, env=unit_env)
runtime = [patched / "runtime/core/window.c", patched / "runtime/core/data_init.c", patched / "runtime/plugins/engine.c"]
includes = [patched / "compat", patched / "runtime/core", patched / "runtime/plugins", upstream / "port/compat", HERE / "tests/support",
            upstream / "port/runtime/core", upstream / "port/runtime/plugins", upstream / "sdk/include"]
include_args = [arg for include in includes for arg in ("-I", str(include))]

# Always read the native baseline from the pinned Git object, so using the same
# checkout for --upstream and --patched cannot become a tautological comparison.
# This compares runtime source branches, not Windows execution on Linux/Wasm.
with tempfile.TemporaryDirectory(prefix="days-native-branches-") as baseline_dir:
    for source in runtime:
        rel = source.relative_to(patched)
        original = subprocess.check_output([
            "git", "-C", str(upstream), "show",
            "2037f79a171524eafcf40a1d641b16da14667a64:port/" + rel.as_posix(),
        ], text=True)
        baseline = Path(baseline_dir) / rel
        baseline.parent.mkdir(parents=True, exist_ok=True)
        baseline.write_text(original)
        outputs = [subprocess.check_output([args.cc, "-E", "-P", "-D__declspec(x)=", *include_args, str(candidate)], text=True)
                   for candidate in (baseline, source)]
        if re.sub(r"\s+", "", outputs[0]) != re.sub(r"\s+", "", outputs[1]):
            raise AssertionError(f"native-default preprocessed source changed: {rel}")
print("Windows-default runtime branches preprocess identically", flush=True)

for mode in ("ordered", "reversed", "empty"):
    with tempfile.TemporaryDirectory(prefix="days-static-registry-") as temp:
        temp = Path(temp)
        fixtures = [] if mode == "empty" else [HERE / "tests/tu_alpha.c", HERE / "tests/tu_beta.c"]
        if mode == "reversed":
            fixtures.reverse()
        registry = write_registry(fixtures, temp / "registry.c")
        common = ["-std=c17", "-O2", "-DKHDAYS_BROWSER_STATIC_REGISTRY=1", *include_args]
        if mode == "empty":
            common.append("-DTEST_EMPTY_REGISTRY=1")
        if mode == "reversed":
            common.append("-DTEST_REVERSED_ORDER=1")
        sources = [str(source) for source in [*runtime, HERE / "tests/runtime_test.c", *fixtures, registry]]
        native = temp / "registry-native"
        subprocess.run([args.cc, "-D__declspec(x)=", *common, *sources, "-o", str(native)], check=True)
        subprocess.run([str(native)], check=True)
        if args.emcc:
            wasm = temp / "registry-test.cjs"
            subprocess.run([args.emcc, "-fms-extensions", *common, *sources, "-sENVIRONMENT=node",
                            "-sSINGLE_FILE=1", "-sWASM_ASYNC_COMPILATION=0", "-sEXIT_RUNTIME=1", "-o", str(wasm)], check=True)
            subprocess.run([args.node, str(wasm)], check=True)
            if args.memory:
                (temp / "khdays_data_area.h").write_text(
                    "#define KHDAYS_IO_VIEW 0x0b100000u\n"
                    "#define KHDAYS_IO2_VIEW 0x0b110000u\n"
                    "#define KHDAYS_DATA_AREA 0x0b200000u\n"
                    "#define KHDAYS_DATA_AREA_END 0x0b200100u\n"
                )
                combined = temp / "combined-test.cjs"
                subprocess.run([
                    args.emcc, "-fms-extensions", *common, "-DKHDAYS_BROWSER_MEMORY=1",
                    "-I", str(temp), str(patched / "runtime/core/memory.c"), *sources,
                    "-sENVIRONMENT=node", "-sSINGLE_FILE=1", "-sWASM_ASYNC_COMPILATION=0",
                    "-sEXIT_RUNTIME=1", "-sALLOW_MEMORY_GROWTH=1", "-sASSERTIONS=2", "-o", str(combined),
                ], check=True)
                subprocess.run([args.node, str(combined)], check=True)
