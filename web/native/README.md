# Native Days browser-port components

These source-only components are a genuine port of parts of the **Days PC port
0.1.5**. They do not execute the complete game, run Pragmicat combat, or make a
browser DS emulator load the native mod. No ROM, extracted content, saves,
generated game C, artwork or compiled game is included.

The core renderer and execution contexts have working WebAssembly implementations:

- `renderer/` compiles upstream's real CPU 3D and 2D renderer and sends its final
  screens to Canvas. A cyan triangle runs through the native polygon rasterizer
  and display composition in the actual Wasm test.
- `context/` replaces the Windows/x64 stack switch with Emscripten Asyncify
  fibers. Nested switches preserve local values and their placement within the
  supplied game-window C stacks. Allocation, deletion and ownership also run in
  actual Wasm, at both `-O1` and `-O2` with stack checks enabled.

The ROM picker/importer one directory above is another independent component.
It validates and extracts a supported local NA/EU cartridge without network
requests. It streams into a `.partial` directory that the native game ignores
and makes the extraction visible with a rename after all files are written.

[`mod/`](mod/README.md) links the nine Pragmicat combat modules into the native
port's built-in plugin table. It embeds the authored Lua and mission
configuration in Practice and separates saves by cartridge and mode. A fresh
Practice profile starts with Endless Heartless selected. The WebAssembly smoke
test checks the production plugin descriptor, C mode flag, bundled files and
profile setup without booting the game.

[`memory-integration/`](memory-integration/README.md) patches the v0.1.5 native
runtime to use the compact map with the existing DS-sized heap option.
Production window, memory, SDK and Lua access run under Wasm in the included
test with the real Asyncify fibers. Full game validation still needs to check
code that assumes mirrors retain their original address identity and raw
pointer operations that cross mappings.

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

Upstream's `port/CMakeLists.txt` targets `ASM_MASM` and Windows x64. The browser
target needs the built-in plugin and the native Lua host. The COFF
`.khdi/.khfr/.khrt` section enumeration needs explicit static registries,
including all generated game routines.

The DS window is reserved through `0x80000000` (2 GiB), including 1.5 GiB of
sparsely committed heaps. `runtime/core/memory.c` uses Windows virtual-memory
aliases, demand-commit exception handlers and guarded custom stacks. Wasm cannot
mirror linear-memory pages that way. A compact mapping requires auditing direct
`khdays_window + address` assumptions, reverse references, interior pointers,
header handles and ranges crossing mirror boundaries. An eager 2 GiB allocation
alone does not preserve aliases and has not been presented as a working port.

The [compiler port](compiler/README.md) adds Emscripten preprocessing and a
wasm32 ABI profile to the upstream source generator. Explicit static registries
preserve data initializers, DS function references and native SDK hooks. Tests
exercise an original combo routine through the patched generator and validate
layout and return conventions. The indexed short-call repair preserves output
and report order across 600 upstream inputs and four targeted fixtures.

The [browser host](host/README.md) and [native entry](start/README.md) implement
cooperative game timing, input, frame delivery and PCM playback. `boot.mjs`
prepares the local extraction and selected profile, then invokes the real game
entry point when the player presses Play. The ready event waits for the first
native frame. The complete generated program and runtime link still need
verification, followed by a real cartridge boot.

The [Lua sandbox](lua/README.md) loads the authored Practice configuration and
selects Endless Heartless under Wasm. The [save backend](save-menus/README.md)
runs the original slot checks and rolls back a rejected write. The
[menu port](menus/README.md) preserves the original native game menus and
measures day-card timing on the browser clock.
The [cartridge integration test](rom-tree/README.md) exercises the production
ROM reader, including LZ11 files, mod discovery and FAT updates, using a
synthetic extraction. The [offline transport](offline-net/README.md) lets the
native session reject wireless multiplayer while retaining its solo path.
The [profile services](profile-services/README.md) apply the active local
profile's language, mixer levels and save settings before firmware boot.

The browser memory patch uses the same original DS arena heaps that the PC port
selects with `KHDAYS_DS_MEMORY=1`. Testing real fields with the mod will establish
whether those heaps leave enough room for the game's resource cache and
animated combat.

## Assemble the browser source tree

Apply the patches in order to a disposable checkout of the upstream pin.
Initialize its pinned decompilation submodule before source generation.

1. `compiler/upstream-compiler.patch`
2. `compiler/index-short-calls.patch`
3. `memory-integration/runtime-memory.patch`
4. `compiler/registry/static-registry.patch`
5. `compiler/registry/prepare-integration.patch`
6. `start/runtime-start.patch`
7. `start/runtime-io-log.patch`
8. `start/runtime-display.patch`
9. `sound/runtime-sound.patch`
10. `save-menus/runtime-save-menus.patch`
11. `menus/runtime-browser-menus.patch`
12. `missions/runtime-missions.patch`
13. `asset-services/runtime-assets.patch`
14. `offline-net/runtime-browser-offline-net.patch`
15. `runtime-input/runtime-input.patch`
16. `compiler/discarded-wasm-returns.patch`
17. `compiler/entity-detach-void-contract.patch`
18. `compiler/forward-query-void-consumer.patch`
19. `profile-services/runtime-browser-profile-defaults.patch`
20. `compiler/key-repeat-quotient-lowword.patch`
21. `compiler/pxi-init-void-contract.patch`
22. `compiler/irq-eight-void-veneers.patch`
23. `compiler/sleep-alarm-void-contract.patch`
24. `compiler/card-transfer-contracts.patch`
25. `compiler/allocator-free-contracts.patch`
26. `compiler/vram-callback-contracts.patch`
27. `compiler/sdk-state-wrapper-contracts.patch`
28. `compiler/font-splitter-contract.patch`
29. `compiler/signed-division-quotient.patch`

Run the upstream generator with the Emscripten SDK active:

```sh
python3 port/tools/prepare.py --out build/port/gen-browser \
  --preprocessor clang --abi-profile wasm32 --compiler /path/to/emcc --jobs 6
```

Wait for successful generation and a new `sources.cmake` listing wasm32 sources.
`PragmicatGameCode.cmake` adds those objects and the registries.
`start/PragmicatBrowserHost.cmake` adds the native entry, engine and display.
`PragmicatBrowserServices.cmake` adds the cartridge reader, save menus, native
mission builder, sound library and Lua game service. Pass the SDL3 target and
the upstream `stb` and `dr_libs` pins documented in [sound](sound/README.md).
`mod/PragmicatBrowser.cmake` embeds the mod and Practice files. These groups are
pieces of the browser executable. The remaining runtime services must link
before producing a playable module. Keep generated output local. The
one-function baseline probe above expects unpatched upstream headers. Use the
compiler port's tests for the patched generator.

For the first local target, SDL3 release 3.4.12 at
commit `f87239e71e42da91ca317a12eefb82cfbf3393eb` builds as a static Emscripten
library with its video, GPU, renderer, audio, joystick, haptic and HIDAPI
subsystems disabled. The browser host has its own display, audio and input
implementations. The [sound bridge](sound/README.md) preserves the original
native mixer and queues replacement decoding on the browser thread.
The [asset services](asset-services/README.md) keep the original VFS, PNG
decoder and texture registry with browser-compatible paths and synchronization.
