#!/usr/bin/env python3
"""Verify the ABI helper and execute two synthetic rewritten C TUs as Wasm.

No ROM or generated game source is used or persisted. All emitted C/IR/binaries
come from the small authored fixture below and live in a temporary directory.
"""
import argparse
import importlib.util
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser()
parser.add_argument("--upstream", type=Path, required=True)
parser.add_argument("--emcc", default="emcc")
parser.add_argument("--node", default="node")
parser.add_argument("--profile-only", action="store_true")
args = parser.parse_args()
upstream = args.upstream.resolve()
sys.path.insert(0, str(upstream / "port/tools"))

suite = unittest.defaultTestLoader.discover(str(HERE / "tests"), pattern="test_*.py")
if not unittest.TextTestRunner(verbosity=1).run(suite).wasSuccessful():
    raise SystemExit(1)
spec = importlib.util.spec_from_file_location("upstream_references_test", upstream / "port/tools/references_test.py")
legacy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(legacy)
if not unittest.TextTestRunner(verbosity=1).run(unittest.defaultTestLoader.loadTestsFromModule(legacy)).wasSuccessful():
    raise SystemExit(1)
if args.profile_only:
    raise SystemExit(0)

from references import rewrite_text
from references.build import data_area_header
from references.emit import Index
from references.layout import Layout
from references.parse import parse
from references.types import TypePass
from references.wasm_abi import classify

records = """
struct R1 {unsigned char a;};
struct R2 {unsigned short a;};
struct R4 {int a;};
struct R4Multi {char a,b,c,d;};
struct R8 {int a,b;};
struct R8Single {long long a;};
struct R12 {int a,b,c;};
struct RFloat {float a;};
struct RDouble {double a;};
struct RNested {struct R4 a[1];};
"""
cases = [
    ("1", "R1", "{x}"), ("2", "R2", "{x}"), ("4", "R4", "{x}"),
    ("4multi", "R4Multi", "{x,x+1,x+2,x+3}"), ("8", "R8", "{x,x+1}"),
    ("8single", "R8Single", "{0x100000000ll+x}"), ("12", "R12", "{x,x+1,x+2}"),
    ("float", "RFloat", "{x+0.5f}"), ("double", "RDouble", "{x+0.25}"),
    ("nested", "RNested", "{{{x}}}"),
]
prototypes = "\n".join(f"struct {record} {prefix}{name}(int x);" for name, record, _ in cases for prefix in ("make", "via"))
header = records + prototypes + "\n"
definitions = records + "\n" + "\n".join(
    f"struct {record} make{name}(int x) {{struct {record} value={init}; return value;}}"
    for name, record, init in cases
)
callers = header + "\n" + "\n".join(
    f"struct {record} via{name}(int x) {{struct {record} (*fn)(int)=make{name}; return fn(x);}}"
    for name, record, _ in cases
)
names = {prefix + name for name, _, _ in cases for prefix in ("make", "via")}
index = Index(game_functions=names, game_objects=set())

with tempfile.TemporaryDirectory(prefix="days-wasm-abi-") as work:
    work = Path(work)
    (work / "abi_cases.h").write_text(header)
    rewritten = []
    for name, source in (("definitions", definitions), ("callers", callers)):
        output = work / (name + ".c")
        output.write_text(rewrite_text(source, "src/abi_" + name + ".c", index, file_key=name, routes=True))
        rewritten.append(output)
    area, _ = data_area_header({}, ["make" + name for name, _, _ in cases])
    (work / "khdays_data_area.h").write_text(area)
    # Collect only the known emitter declaration, retaining each original
    # typed record and its linkage. This is a bounded test harness table.
    record_names = sorted(set(re.findall(r"const struct khdays_function_record\s+([A-Za-z_][A-Za-z_0-9]*)\s*=", rewritten[1].read_text())))
    if len(record_names) != len(cases):
        raise AssertionError(f"expected {len(cases)} actual emitted function records, got {len(record_names)}")
    lookup = "\n".join(f"extern const struct khdays_function_record {name};" for name in record_names)
    lookup += "\nstatic const struct khdays_function_record *const fixture_records[] = {\n"
    lookup += "\n".join("    &" + name + "," for name in record_names) + "\n};\n"
    lookup += """
unsigned char *khdays_window;
khdays_ref khdays_reference(const void *host) {return (khdays_ref)((uintptr_t)host-(uintptr_t)khdays_window);}
khdays_function_pointer khdays_function(khdays_ref reference) {
    for (unsigned i=0; i<sizeof(fixture_records)/sizeof(fixture_records[0]); ++i)
        if (fixture_records[i]->reference==reference) return fixture_records[i]->function;
    fprintf(stderr,"missing authored ABI fixture function %u\\n",reference); abort();
}
"""
    (work / "fixture_record_lookup.h").write_text(lookup)
    includes = [work, upstream / "port/compat"]
    common = [args.emcc, "-std=c17", "-fms-extensions", "-fpack-struct=4"]
    common += [arg for include in includes for arg in ("-I", str(include))]
    ir = work / "definitions.ll"
    subprocess.run([*common, "-O0", "-S", "-emit-llvm", str(rewritten[0]), "-o", str(ir)], check=True)
    ast = parse(definitions, "src/abi_definitions.c")
    types = TypePass().run(ast)
    layout = Layout(types)
    ir_lines = ir.read_text().splitlines()
    for name, _, _ in cases:
        line = next(line for line in ir_lines if line.startswith("define ") and f"@make{name}(" in line)
        ret = line.split(f"@make{name}(", 1)[0].split()[-1]
        actual = "sret" if "sret(" in line else "i32" if ret in ("i1","i8","i16","i32") else {"i64":"i64","float":"f32","double":"f64","void":"void"}[ret]
        expected = classify(layout, types.file.ordinary["make" + name].type.result).signature
        if actual != expected:
            raise AssertionError(f"make{name}: model {expected}, actual compiler {line}")
    print("10 aggregate-return classifications match actual Emscripten LLVM IR", flush=True)
    executable = work / "abi-test.cjs"
    subprocess.run([
        *common, "-O2", *(str(source) for source in rewritten), str(HERE / "tests/abi_dispatch_test.c"),
        "-sENVIRONMENT=node", "-sSINGLE_FILE=1", "-sWASM_ASYNC_COMPILATION=0", "-sEXIT_RUNTIME=1",
        "-sASSERTIONS=2", "-o", str(executable),
    ], check=True)
    subprocess.run([args.node, str(executable)], check=True)
