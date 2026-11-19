# Browser runtime entry

`runtime-start.patch` adds an Emscripten branch to Days PC port 0.1.5. The
branch retains its native ROM initialization, mission setup, static plugin
discovery, context switch, CRT boot and game reset. It keeps the working
directory selected by the browser and excludes the Windows console, export
commands and OS page fault diagnostics. Native plugins must be linked into the
game module on this target. The existing safe mode still disables them.
The diagnostic unit uses the WebAssembly stack and native SDK's compiled
routine names in place of PDB data. Its scene and update hooks still
dispatch the original game callbacks and reject unresolved update references.

Apply this patch to a disposable upstream checkout at
`2037f79a171524eafcf40a1d641b16da14667a64` alongside the browser memory,
compiler and static registry patches. Build the generated C from the
`wasm32` reference rewriting profile.

Apply `runtime-io-log.patch` to keep the real ROM and event paths in the
browser target. Card page remapping, chip ID, read modes and grouped byte
counts are shared with the desktop runtime. The browser uses its monotonic
clock for read timing, and its single cooperative host thread updates the
read counters. Browser session tracing follows the existing enable setting
and writes up to 2 MiB to the local console per module. The stall report
retains timing and scene information.

Apply `runtime-display.patch` for the real software compositor, scene wipe,
graphics probe, original DS movie wrapper, boot, language and I/O units. Its browser branch uses the
monotonic clock and cooperative scalar updates for existing frame profiling,
and maps optional diagnostic file writes to the virtual filesystem.

`PragmicatBrowserHost.cmake` adds the patched start, runtime and plugin units,
the Asyncify context and clock, browser input and local player slot, and the
upstream CPU renderer with its Canvas bridge. The target's source list should
include each of those units once. Use the generated `KHDAYS_REWRITTEN` list
for the game and its static registries. Apply the same browser defines to
runtime sources built in separate targets. The full target still needs the
remaining runtime sources, dependencies and browser platform services.

The host requests original two-screen CPU composition and DS movies. Before
calling Emscripten `module.callMain([])`, import game data, prepare the selected
profile, and attach the renderer's `module.onDaysFrame` callback. The first
frame callback confirms native picture output. The
`boot.mjs` adapter also drains the original script notice queue on each
presented frame. Its two accessible lines sit beside the canvas in document
order. A five-second gameplay cue and the latest localized mod warning can
be read together. Both disappear when the session exits. The optional
`module.onDaysRuntimeProblem(where, problem)` callback reports missing game
data or a missing Canvas sink. `module.onDaysRuntimeClosed()` reports a native
host close. The game has an asynchronous runtime lifetime, including a normal
exit or aborted build. The host-close notification alone does not stop game
execution.

## Verify

```sh
git -C /path/to/khdays-port apply /path/to/runtime-start.patch
git -C /path/to/khdays-port apply /path/to/runtime-display.patch
git -C /path/to/khdays-port submodule update --init --depth 1 external/khdays-decomp
git clone --branch release-3.4.12 --depth 1 https://github.com/libsdl-org/SDL.git /tmp/days-sdl3
python3 web/native/start/run_test.py --upstream /path/to/khdays-port --sdl /tmp/days-sdl3
git -C /path/to/khdays-port apply /path/to/runtime-io-log.patch
python3 web/native/start/run_io_log_test.py --upstream /path/to/khdays-port
```

The worktree and SDL3 commit are checked before testing. Emscripten 4.0.23
compiles the actual patched start, runtime, diagnostic and plugin units. Node
executes the Wasm display contract with the original renderer compiled in. The
tests cover start refusal without the framebuffer sink, native two-screen pixel
delivery, error messages and close notification. The diagnostic test exercises
scene/update dispatch and the original invalid update rejection.
Windows preprocessor tokens
match upstream when the browser flag is absent. The preprocessor comparison
uses empty Windows SDK fixture headers. It does not replace a Windows build.
No game data was used, and the complete game did not link or boot in this test.

The second test compiles the actual event scheduler, ROM read accounting and
session log, and runs the last two against synthetic ROM and card callbacks.
It verifies read forwarding, page remapping, grouped counts, settings-driven
logging, and the lifetime console budget.
