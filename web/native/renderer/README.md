# Days browser software renderer adapter

This builds the actual **Yokimitsuro/khdays-port v0.1.5** CPU 3D/2D renderer and
presents its final buffers through Canvas2D. It is a reusable browser-port
component. It does **not** start the Days game, run Pragmicat combat, or emulate a
ROM. No ROM, extracted files, saves, original artwork, or game data are included.

In a linked Pragmicat session the combat module draws its native Drive panel
between the two screens. The game keeps both 256×192 images and the entire
lower touchscreen. The panel uses the same glyphs, charge highlight and
form-change cues as the PC mod. The strip keeps its height when combat pauses
and disappears when the mod disables the meter.

The upstream GPU/HLSL backend is unnecessary for the initial browser path.
`runtime/display/display.c` already draws all 3D and 2D in software when the
host returns zero from `khdays_host_frame_wants()`. Have the browser host return
NULL from `khdays_host_frame_begin()`, width factor 1 and frame scale 1; it then
calls `khdays_host_present(upper, lower)`, implemented by `frame_bridge.c`.
This host callback accepts final **0x00RRGGBB** screens. The adapter converts them
to explicit RGBA bytes before notifying JavaScript. No OS or SDL is needed by
the four upstream CPU renderer files.

## Build and verify

Point to an unmodified upstream v0.1.5 source checkout; upstream sources are not
copied or vendored by this adapter. Native regression needs Python and a C17
compiler; the Canvas regression needs Node:

```sh
python3 tests/run_native.py --upstream /path/to/khdays-port
node tests/canvas-presenter.test.mjs
```

With Emscripten and CMake available:

```sh
emcmake cmake -S . -B build-web -DKHDAYS_PORT_SOURCE=/path/to/khdays-port
cmake --build build-web --parallel
```

Or build and run both Wasm regressions directly, without CMake:

```sh
python3 tests/run_wasm.py --upstream /path/to/khdays-port --emcc /path/to/emcc
```

This checks the real Wasm module's framebuffer callback, Canvas presenter,
actual WebAssembly memory growth, and the CPU triangle/display composition
regression compiled to Wasm.

Outputs are `build-web/days_software_renderer.mjs` and its `.wasm` module. To
attach the future game runtime's real framebuffer:

```js
import createDaysSoftwareRenderer from "./days_software_renderer.mjs";
import { attachDaysCanvas } from "./canvas-presenter.mjs";
const module = await createDaysSoftwareRenderer();
const display = attachDaysCanvas(module, document.querySelector("canvas"));
// The port's game thread calls khdays_host_present; each call draws the canvas.
// Release the presenter when the game session closes:
// display.dispose();
```

The direct build also packages upstream, Emscripten and musl notices and a
source/output SHA-256 manifest. Keep these files with distributed renderer
binaries. The four upstream source files are MIT licensed; they are not the
decompiled game or game assets.

`days_web_render` also accepts the upstream typed polygon/input structures and
executes its real CPU 3D and 2D pipeline; it is useful for eventual native-side
integration. It does not parse game data or synthesize gameplay. The CPU
rasterizer must be called serially, as in the native runtime. Native regression
checks an actual upstream cyan triangle against a red 3D rear plane composed
into engine A, engine B's green backdrop, screen swapping, RGBA byte order, and
rejected invalid input.

## Remaining game-runtime work

A complete browser game still needs the upstream Windows sparse/aliased memory
window converted to linear memory, cooperative stack contexts ported from
`context_x64.asm`, generated COFF function/hook registries replaced, portable
runtime timing/file/platform services, local ROM extraction, browser controls,
audio and saves, and the actual Pragmicat plugin linked into the runtime.
This component removes the graphics-card API blocker, not those game-runtime
requirements. Native renderer checks do not establish browser performance,
actual game playback, controller feel, or mod behavior.
