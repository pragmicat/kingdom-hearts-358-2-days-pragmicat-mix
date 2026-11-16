# Native Days browser-port components

These source-only components are a genuine port of parts of the **Days PC port
0.1.5**. They do not execute the complete game, run Pragmicat combat, or make a
browser DS emulator load the native mod. No ROM, extracted content, saves,
generated game C, artwork or compiled game is included.

Two platform barriers have working WebAssembly implementations:

- `renderer/` compiles upstream's real CPU 3D and 2D renderer and sends its final
  screens to Canvas. A cyan triangle runs through the native polygon rasterizer
  and display composition in the actual Wasm test.
- `context/` replaces the Windows/x64 stack switch with Emscripten Asyncify
  fibers. Nested switches preserve local values and their placement within the
  supplied game-window C stacks. Allocation, deletion and ownership also run in
  actual Wasm, at both `-O1` and `-O2` with stack checks enabled.

The ROM picker/importer one directory above is another independent component.
It validates and extracts a supported local NA/EU cartridge without network
requests. None of these tests are game playback or controller validation.

`experiments/` records two further bounded tests. A checked compact-memory map
runs with the real fibers, including RAM mirrors, independent DTCM, archive
header references, upper aliases for DS-memory mode, high heap fragments and a
stack local accessed through a DS reference across suspension. It is not wired
into upstream; its canonical reverse-alias semantics require an audit.

The codegen probe takes **one original native battle routine** from the pinned
decomp checkout, preprocesses it with clang-cl, rewrites it using upstream's
actual four-byte reference writer, and compiles/executes it as Wasm. It verifies
combo-window boundaries and swaps/restores its actual generated hook record
through an explicit static reference. Generated game C remains temporary. This
proves one translation unit and hook record, not the whole-game ABI or registry.

## Reproduce

Pinned upstream is `Yokimitsuro/khdays-port` tag `v0.1.5`, commit
`2037f79a171524eafcf40a1d641b16da14667a64`. `UPSTREAM.json` records hashes of every
upstream renderer source/header and the context API used here; builds reject
changed dependencies. Tests used Emscripten **4.0.23**, Python 3.12, Node
**24.22.1**, and GCC 14.2 for the native renderer. Build outputs stay local.

```sh
git clone --branch v0.1.5 --depth 1 https://github.com/Yokimitsuro/khdays-port.git /tmp/khdays-port
python3 web/native/renderer/tests/run_native.py --upstream /tmp/khdays-port
node web/native/renderer/tests/canvas-presenter.test.mjs
python3 web/native/renderer/tests/run_wasm.py --upstream /tmp/khdays-port --emcc /path/to/emcc
python3 web/native/context/run_test.py --upstream /tmp/khdays-port --emcc /path/to/emcc
python3 web/native/experiments/memory/run_test.py --upstream /tmp/khdays-port --emcc /path/to/emcc
# The optional original-code probe also needs the pinned decomp submodule:
git -C /tmp/khdays-port submodule update --init --depth 1 external/khdays-decomp
python3 -m pip install pycparser==3.0
python3 web/native/experiments/probe_game_codegen.py --upstream /tmp/khdays-port --emcc /path/to/emcc
```

The real Wasm renderer test also checks Canvas delivery after actual linear
memory growth. Renderer outputs include required upstream, Emscripten and musl
notices and source/output SHA-256 provenance. Keep those notices with any
redistributed component binaries. `UPSTREAM_LICENSE.txt` preserves the upstream
MIT notice. Emscripten's generated runtime uses its MIT/University of Illinois
license and musl's included notices. This repository currently retains sources.

## Remaining whole-game gates

Upstream's `port/CMakeLists.txt` targets `ASM_MASM` and Windows x64. A browser
target must statically link the actual Pragmicat plugin and its Lua practice
script. The Windows DLL loader and COFF `.khdi/.khfr/.khrt` section enumeration
need explicit static registries, including all generated game routines.

The DS window is reserved through `0x80000000` (2 GiB), including 1.5 GiB of
sparsely committed heaps. `runtime/core/memory.c` uses Windows virtual-memory
aliases, demand-commit exception handlers and guarded custom stacks. Wasm cannot
mirror linear-memory pages that way. A compact mapping requires auditing direct
`khdays_window + address` assumptions, reverse references, interior pointers,
header handles and ranges crossing mirror boundaries. An eager 2 GiB allocation
alone does not preserve aliases and has not been presented as a working port.

The full generated source pass also assumes MSVC `cl /E` through
`port/tools/references/build.py:_init` and `preprocess.py`. A clang-cl
preprocessing experiment executes one rewritten native combo routine and its
explicit hook record, but does not establish that the whole generated program
compiles or links for Wasm.
Clock, I/O traps, audio, filesystem/save lifecycle, browser input and frame-loop
yielding still need real browser implementations and game validation. The
software renderer removes the SDL_GPU/HLSL blocker; it does not remove these.

The smallest initial boot route can use upstream's existing
`KHDAYS_DS_MEMORY=1`: `game_heap.c` then creates the original DS arena heaps and
`memory.c` mirrors the upper RAM regions instead of allocating expanded header
heaps. That may avoid the expanded sparse heaps for an initial ROM-only build.
It still needs integration and validation with the actual mod and resource
cache; no memory-layout change has been applied to the current native game.
