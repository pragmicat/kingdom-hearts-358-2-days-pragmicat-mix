# Days production static metadata registry

This source patch replaces Windows COFF-section enumeration with explicit
generated data, function and hook-route tables under
`KHDAYS_BROWSER_STATIC_REGISTRY`. It targets upstream Days port **v0.1.5**,
commit **2037f79a171524eafcf40a1d641b16da14667a64**. The default Windows section
paths remain unchanged. This is a first-boot dependency, not a complete game
runtime or a playable browser release.

`static-registry.patch` changes four existing production files and adds two:

- `port/compat/khdays_references.h`: shared typed metadata for this build.
- `port/runtime/core/window.c`: numbered-function collection and lookup.
- `port/runtime/core/data_init.c`: module-specific initializer iteration.
- `port/runtime/plugins/engine.c`: named hook-route collection and lookup.
- New `port/compat/static_registry.h` and `port/tools/static_registry.py`.

Each table references the original globally visible records. Their private
initializer and function targets remain in the original translation units.
The runtime uses the actual named record types, with no casts between anonymous
record layouts. Empty tables have a physical NULL slot and explicit zero count.
Legitimate repeated numeric function references from different TUs are kept.

Data entries retain the supplied build source order and per-file declaration
order. They are not alphabetized: overlapping aliases can otherwise initialize
to different bytes. Function and route lookup retains the production runtime's
own sorting, caching and hook replacement logic.

## Preparation and compilation

Apply this after the compiler and browser-memory patches. The verified complete
application sequence was compiler, short-call indexing, browser memory, this
registry patch, then the separate prepare integration patch.

After `rewrite_game` returns its complete **ordered current source list**:

```python
from static_registry import write_registry
registry = write_registry(rewritten, out / "khdays_static_registry.c")
rewritten.append(str(registry))
```

Use that exact list, not a directory glob containing stale outputs. Compile the
registry, rewritten C and production runtime with
`-DKHDAYS_BROWSER_STATIC_REGISTRY=1`, `-fms-extensions` and the existing
`port/compat` include directory. Emscripten **4.0.23** accepts the existing
emitter's `__declspec(allocate(...))` metadata declarations; the emitter itself
does not need to change. The browser now uses explicit references rather than
enumerating the linker sections.

## Verification

The test suite compiles actual patched `window.c`, `data_init.c` and `engine.c`
with two authored translation units, then repeats with reversed source order
and with no records. Six generator tests pass. All three modes pass native and
actual Wasm execution. The macro-disabled production runtime files preprocess
identically to the original pinned Git source, loaded with `git show`; Windows
execution itself has not been observed.

Checks cover all three metadata kinds, module filtering, source-ordered
overlapping initializers, unsorted and repeated numeric function references,
calls through those references, actual `KhEngine::replace`, suspension/resume,
two-owner unwind, wrong-thread rejection and repeated empty-table lookups.

The combined test also passes with both `KHDAYS_BROWSER_MEMORY=1` and
`KHDAYS_BROWSER_STATIC_REGISTRY=1`. It compiles production `memory.c` too and
checks real compact DS main-RAM initializer writes, SDK pointer/reference
round trips, lookup/hook behavior and unmapping. In this mode the fixtures use
canonical DS addresses. In standalone modes they deliberately use a small flat
memory buffer. The combined memory test registers the context stack allocator
through a stub; it does not create or validate fibers. Unrelated diagnostics,
plugin identity/thread readiness and host services are bounded test stubs.

The successful combined command in this workspace was:

```sh
python3 tests/run_tests.py \
  --upstream /workspace/scratch/f84e6b3f9f1e/days-web-compiler-recovery \
  --patched /workspace/scratch/f84e6b3f9f1e/days-compiler-apply-check \
  --memory \
  --emcc /workspace/scratch/f84e6b3f9f1e/emsdk-days-browser/upstream/emscripten/emcc
```

The runner imports its generator only from the explicitly applied `--patched`
checkout after argument parsing. Generator unit tests inherit that location; no
patched upstream copies are bundled. Its native-default comparison always reads
the pinned Git object, so `--upstream` and `--patched` may safely identify the
same checkout. The private `tests/support/windows.h` is a fixture include and
must not enter a production browser target. No ROM, assets, saves, generated game C or compiled game
binary belongs in this source bundle. Authored fixture registries and test
executables are temporary and deleted.

Full game boot, actual mod combat, real browser/controller playback and Windows
COFF behavior remain separate verification work.

## Included prepare integration

Apply `prepare-integration.patch` after the compiler patch and the runtime
registry patch. The Clang/Wasm path appends the explicit registry to
`KHDAYS_REWRITTEN` and tags `sources.cmake` with `KHDAYS_ABI_PROFILE=wasm32`.
It invalidates the old browser source list when a new attempt starts and writes
the new list atomically after successful generation and validation. A failed
Wasm attempt cannot expose the previous link plan as a new success. The two
`prepare_gate_test.py` cases cover that failure and unchanged native behavior:

```sh
python3 tests/prepare_gate_test.py --upstream /path/to/applied-checkout
```

Root's clean-checkout application order was verified without conflicts:

1. Compiler `upstream-compiler.patch`.
2. Cold-build `index-short-calls.patch` (separate root-owned artifact).
3. Browser-memory `runtime-memory.patch`.
4. This `static-registry.patch`.
5. This `prepare-integration.patch`.

The integrated CMake recipe should include the generated `sources.cmake`,
require `KHDAYS_ABI_PROFILE` to equal `wasm32`, and compile every
`KHDAYS_REWRITTEN` entry plus `ds_functions.c` and `ds_names.c`. Generated game
C needs `-fpack-struct=4` and `-fms-extensions`, the `port/compat` and generated
include directories, and the browser memory/static registry definitions. Keep
packing flags scoped to generated game code. The complete executable also
needs its browser host, actual statically linked Pragmicat plugin, runtime
services and final `-sASYNCIFY` link. No complete game link or first boot is
claimed by these tests.
