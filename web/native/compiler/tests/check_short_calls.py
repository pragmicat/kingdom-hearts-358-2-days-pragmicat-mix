"""Check that the indexed ABI repair preserves edits and report ordering."""
import argparse
import importlib.util
from pathlib import Path
import subprocess
import sys
import time
from types import ModuleType

PIN = "2037f79a171524eafcf40a1d641b16da14667a64"
REPAIR = "port/tools/abi_repair.py"

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", type=Path, required=True)
    parser.add_argument("--decomp", type=Path, required=True)
    args = parser.parse_args()
    checkout = args.upstream.resolve()
    baseline = subprocess.check_output(
        ["git", "show", f"{PIN}:{REPAIR}"], cwd=checkout, text=True)
    original = ModuleType("original_abi_repair")
    sys.modules[original.__name__] = original
    exec(compile(baseline, REPAIR, "exec"), original.__dict__)
    spec = importlib.util.spec_from_file_location("indexed_abi_repair", checkout / REPAIR)
    indexed = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = indexed
    spec.loader.exec_module(indexed)

    sources = sorted((args.decomp / "src").rglob("*.c"))
    if len(sources) < 600:
        raise SystemExit("Initialize the pinned Days decompilation submodule.")
    # Spread the sample over the whole tree so it includes different overlays.
    step = len(sources) // 600
    texts = {str(path.relative_to(args.decomp)): path.read_text(encoding="utf-8", errors="replace")
             for path in sources[::step][:600]}
    texts.update({
        "fixture-one.c": "static int enough(int a, int b) { return a + b; }\n"
                         "int local(void) { return enough(1); }",
        "fixture-two.c": "static int enough(int a, int b, int c) { return a + b + c; }\n"
                         "int nested(void) { return enough(enough(1)); }",
        "fixture-three.c": "int exported(int a, int b) { return a + b; }",
        "fixture-four.c": "extern int exported(int a);\n"
                          'int caller(void) { /* exported(0) */ return exported(1); }',
    })
    start = time.perf_counter()
    expected = original.pad_short_calls(texts)
    original_seconds = time.perf_counter() - start
    start = time.perf_counter()
    actual = indexed.pad_short_calls(texts)
    indexed_seconds = time.perf_counter() - start
    if actual != expected or len(actual[1]) < 3:
        raise SystemExit("The indexed pass changed the repaired source or report.")
    print(f"Identical edits and report across {len(texts)} inputs "
          f"({original_seconds:.2f}s before, {indexed_seconds:.2f}s indexed).")

if __name__ == "__main__":
    main()
