# Original Days runtime input in Wasm

`runtime-input.patch` ports the existing `runtime/input/input.c` to Emscripten
under `KHDAYS_BROWSER`. It retains the real Days frame latch, script parser,
input filters, ARM7 button word and hook flow. It adds browser-only libc
equivalents for Microsoft's owned string copy, reentrant tokenization,
case-insensitive control names and numeric scanning. The native default path
is unchanged. The patch touches **only** `port/runtime/input/input.c`.

Target: upstream Days port v0.1.5, commit
`2037f79a171524eafcf40a1d641b16da14667a64`, Emscripten 4.0.23. Unpatched
`input.c` reproduced nine undeclared CRT/integer-to-pointer errors in the Wasm
compile. The patched source compiles without those aliases on the command line.
This is actual recompilation input code, not an emulator control replacement.

## Apply and reproduce

Apply the source patch to the pinned developer checkout. The integration tests
also require the existing browser-memory patch and the real browser host input
adapter from the Pragmicat source tree:

```sh
git -C /path/to/upstream apply /path/to/runtime-input.patch
python3 tests/run_tests.py \
  --upstream /path/to/upstream \
  --host /path/to/pragmicat/web/native/host \
  --emcc /path/to/emsdk/upstream/emscripten/emcc
```

The runner resolves all production code from those explicit paths. It creates
only temporary generated fixture headers, objects and Wasm executables. No
upstream runtime copies, ROM, assets, saves or compiled game are included.

Observed checks:

- All five original `runtime/input` C units compile as Wasm with
  `-Wall -Wextra -Werror`.
- The macro-disabled input source preprocesses identically to the pinned Git
  baseline; Windows execution itself was not observed.
- Seven actual Wasm modes pass using production `input.c`, `controls_hooks.c`,
  `analog.c`, `sequencers.c`, `walk.c`, browser host input and compact DS memory.
- Brief A/X/Y taps reach exactly the original latch, including X/Y in actual
  RAM at `0x027fffa8`; later reads remain instantaneous.
- Release/repress, held buttons, host UI ownership, touch bounds, movie gates,
  filter order and post-plugin ARM7 writes remain intact.
- The actual parser handles mixed-case keys/actions, numeric touch/stick fields,
  scene/update anchors and its two-read release rule. A malformed numeric
  script retains the original error status. Diagnostic scripts intentionally
  bypass host UI ownership, as in the upstream headless workflow.
- Original analog hooks use the previous snapshot, respect a missing native
  heading and suppressed directions, and retain their selection hold behavior.

## Build wiring and limits

The real browser executable needs these production sources:

```cmake
target_sources(game PRIVATE
  "${port}/runtime/input/input.c"
  "${port}/runtime/input/controls_hooks.c"
  "${port}/runtime/input/analog.c"
  "${port}/runtime/input/sequencers.c"
  "${port}/runtime/input/walk.c")
target_include_directories(game PRIVATE "${port}/runtime/core")
```

The browser host adapter includes `clock.h`, so that core include directory is
required in addition to the existing port and host include paths. Use
`KHDAYS_BROWSER=1` and `KHDAYS_BROWSER_MEMORY=1`; existing Emscripten and full
runtime link requirements still apply. This patch does not replace the original
menu, plugin, camera, model or network implementations needed by a full game.

Tests use controlled stubs for unrelated menu/plugin/movie/camera/network and
diagnostic services. They check the real input dispatcher and its effects;
they do not validate those unrelated systems or imported walk animations. The
memory stack-source registration is a test stub; no fibers are created here.

The analog hook test explicitly supplies the native DS direction bits before
testing analog heading replacement. Browser host stick-to-D-pad synthesis and
visibility/menu ownership remain separate adapter responsibilities; this
runtime-only patch makes no claim to repair them. Full native game boot,
controller feel and browser gameplay remain unverified.
