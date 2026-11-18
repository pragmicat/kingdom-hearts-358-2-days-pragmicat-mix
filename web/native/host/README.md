# Browser clock, input and local player services

Source-only replacements for **khdays-port v0.1.5**, commit
`2037f79a171524eafcf40a1d641b16da14667a64`. These link the actual upstream
`clock.h`, `host.h`, `press_edges.h` and `instance.h`; there is no DS emulator or
separate imitation of the game ABI. `UPSTREAM_HOST.json` pins these interfaces.
This is a runtime component, not a playable Days build.

`clock_emscripten.c` replaces `runtime/core/clock.c`. It derives the original
33,513,982 Hz DS cycle count from Emscripten's monotonic clock, excludes nested
settings/movie/visibility holds, retains latched wake events, and suspends the
calling game fiber through Asyncify while waiting. It does not spin or block the
browser event loop. Wakes are observed after at most a requested 4 ms sleep in an
unthrottled event loop; browser scheduling can delay that. The rate stays 1.0.
Windows' DWM screen-lock tuning is deliberately absent because a browser has no
matching compositor timestamp. Browser cadence/audio performance needs actual
in-game measurement.

`input_emscripten.c` implements the upstream host input callbacks. A quick tap
between game samples survives through upstream's real press-edge collector.
The [analog bridge](ANALOG_INPUT.md) combines physical buttons and the stick's
native D-pad sectors. It supplies raw movement tilt and uses a radial dead zone
for the camera. Focus loss clears physical input while preserving menu
ownership. Visibility owns hold bit 16, so showing the tab cannot release
settings or movie holds. These callbacks run on one cooperative host thread.

`browser-input.mjs` sends DOM keyboard, standard-mapped gamepad and lower-screen
stylus events to the C bridge without packing a JavaScript copy of a native
struct. It captures keyboard gameplay only while the canvas has focus, combines
keyboard/gamepad buttons, handles disconnects and blur, and releases listeners,
keys and RAF polling on disposal. Canvas coordinates assume the renderer's
256×384 stacked pair of DS screens. These are initial bindings, not verified
controller-feel tuning:

| DS input | Keyboard |
| --- | --- |
| A / B / X / Y | J / K / U / I |
| D-pad | Arrow keys |
| L / R | Q / E |
| Start / Select | Enter / Shift |
| Touch | Pointer on the lower screen |

`instance_emscripten.c` gives this Wasm module local player slot 1 and relative
`saves`, matching the Windows slot-1 path. The startup helper must select/hydrate
the ROM/profile-specific working directory and saves mount before native main.
It does not claim to detect other browser tabs or provide multiplayer. Its
fallback firmware nickname is the nonidentifying `Player0000`.

## Integration

Replace upstream Windows clock/instance sources with these C files. Compile and
link the **entire game** with `-sASYNCIFY`; use the existing browser context
implementation and actual upstream headers. Do not add `-sASYNCIFY_ONLY` unless
all indirect game/fiber calls have been audited. The memory window, generated
reference registry, complete mod/Lua host, audio, file/save I/O, startup and
presentation still need to be supplied by their respective port components.

Attach input after module initialization and dispose it before releasing the
module. Keep each module's bridge with that module:

```js
const input = attachDaysInput(module, canvas);
// Launch native main only after the ROM import and save hydration.
input.focus();
// When native execution has stopped:
input.dispose();
```

## ROM-free verification

From the repository root, with the pinned upstream checkout and SDK available:

```sh
node web/native/host/tests/browser-input.test.mjs
python3 web/native/host/run_test.py --upstream /path/to/khdays-port --emcc /path/to/emsdk/upstream/emscripten/emcc
```

The Wasm smoke runs at `-O1` and `-O2` with assertions and stack checks. It checks
elapsed DS cycles, repeated/nested holds, visibility ownership, latched and timed
wakes, real upstream snapshot and press-edge behavior, touch/analog bounds,
local-player services, and browser timer callbacks while a real Days Asyncify
fiber is suspended. The JavaScript event test checks focus, duplicate bindings,
touch ownership, combined input sources, disconnects, visibility and teardown.
Neither test boots Days, validates browser performance or establishes actual
combat feel. No ROM, saves, generated game C or compiled outputs are retained.

## Native mixer and WebAudio

`audio_emscripten.c` supplies real `khdays_host_audio`,
`khdays_host_audio_float`, `khdays_host_sound_start` and the upstream
`sound_output.h` functions. It replaces `host/app/sound_output.c`. The initial
browser output selects upstream's unchanged DS-exact mixer at **32,728 Hz**;
WebAudio resamples that PCM for the device. It does not substitute effects or
music. Host volume settings remain in the native bridge and are applied once.
The float callback is also implemented, but only forwards once the device's
actual rate is reported; this initial browser path does not select that mixer.
HD video/theater audio is outside this DS-first output path.

Native `khdays_host_sound_start` idempotently opens the sink and registers one
1 ms Emscripten interval. Each callback synchronously calls the real
`khdays_sound_bridge_advance(khdays_clock_cycles())`. It runs on the cooperative
host thread when Asyncify gives the browser its event loop. The bridge/pump and
`Module.onDaysAudio` **must not suspend or re-enter game execution**. The native
bridge's existing game-event catch-up remains needed during long native work.
Browser scheduling, contention and underruns need actual game measurement.
`khdays_sound_output_close`/`days_web_audio_stop` cancel this interval. The
interval leaves native exit enabled and clears itself after an abort or
terminal exit. The linked [sound bridge](../sound/README.md) implements the
original driver's browser synchronization and replacement decode queue.

`browser-audio.mjs` attaches `Module.onDaysAudio(pointer, frames, rate, format)`
and `Module.onDaysAudioControl(action, enabled)`. PCM format 0 is signed 16-bit
interleaved stereo; format 1 is float32. Pointer/alignment/count/rate/heap bounds
are checked for every packet, reading the current heap after memory growth.
Samples are copied into separate AudioBuffers before the native mixer reuses
its storage. Native blocks are split into at most 512 frames. A 30 ms startup
lead matches upstream's output lead, and packets are dropped when queued time
already exceeds 100 ms (plus at most one block). Pause, mute, hidden-tab and
suspended-device boundaries clear stale queued audio. Page/native pause owners
are independent.

Attaching the sink or receiving PCM does not construct an AudioContext. After
local ROM validation/import/profile hydration, the page must provide a fresh
**Play / Enable sound** gesture and call `resume()` directly from it. An earlier
file-selection gesture may expire during import. Visibility changes never call
AudioContext.resume automatically.

```js
const audio = attachDaysAudio(module, {onStatus, onError});
// Called directly by a fresh Play/Enable sound click:
await audio.resume();
// The native runtime can now start, or resume sound for an existing session.
// On native exit, after its sound service is stopped:
await audio.dispose();
```

The sink also offers `setVolume(0..1)`, `setPaused(bool)` and read-only queue
statistics. It restores previous module callbacks and removes listeners on
teardown. Device failures can leave gameplay silent; present the `resume()`
error to the player without calling it a successful audio start.

```sh
node web/native/host/tests/browser-audio.test.mjs
python3 web/native/host/run_audio_test.py --upstream /path/to/khdays-port --emcc /path/to/emsdk/upstream/emscripten/emcc
```

The audio Wasm test compiles the **actual pinned upstream** `channels.c` and
`mix_ds.c`, feeds synthetic non-game channel values through the actual mixer,
and verifies the real host PCM callback's exact stereo values/rate/chunking in
the JS sink. It also exercises memory growth, pause/mute, float forwarding,
cooperative pump timing, shutdown and native exit with an active pump. A recording bridge stub only verifies the
pump's ABI/cycle calls; it does not run the ARM7 driver. WebAudio is represented
by a bounded fake device in Node, so these checks do not establish audible game
playback, audio latency or browser device compatibility. No game audio assets or
compiled outputs are retained. `UPSTREAM_AUDIO.json` pins these dependencies.
