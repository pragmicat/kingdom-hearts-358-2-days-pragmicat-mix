# Actual sound-bridge browser portability

`runtime-sound.patch` ports the real **khdays-port v0.1.5**
`runtime/sound/sound_bridge.c` and `sound_mods.c` to a single cooperative
Emscripten host. It also adds browser-only bounds to `sound/replace.c` before
replacement sample allocations. This is source work for the actual native Days
runtime, not an emulator, replacement sound engine or playable game build.
The upstream MIT notice is preserved at `../UPSTREAM_LICENSE.txt`.

The patch applies to public upstream commit
`2037f79a171524eafcf40a1d641b16da14667a64`. Native Windows implementation branches
remain in place. The browser branch:

- Uses explicit single-threaded lock/re-entry guards and scalar atomic-operation
  semantics. Pthread builds are rejected. A held sound lock must never span an
  Asyncify suspension, and host PCM callbacks must not re-enter the driver.
- Keeps the actual ARM7 driver bridge, reply ordering, exact DS mixer, PC mixer,
  stream reader and category/volume logic. Profiling uses Emscripten's monotonic
  clock instead of Windows performance-counter calls.
- Uses POSIX `stat`/directory iteration for replacement discovery in MEMFS/IDBFS.
- Decodes one queued replacement per timer callback after the enqueueing sound
  call returns. The queue is limited to 64 entries and preserves immediate-use
  priority and deduplication. One file's decode is synchronous and may still
  take noticeable time; no unmeasured performance claim is made.
- Rejects encoded files above 8 MiB and decoded PCM above 8 MiB, before PCM
  allocation, for WAV, FLAC and Ogg. Browser replacement rates above 192 kHz are
  also refused. A refused replacement leaves the original sound eligible.
  These limits affect optional sound replacements, not the ROM's native audio.
- Targets 64 MiB of cached replacement PCM by evicting unused files. Files in
  active use are retained, so this cache target is not a total-memory promise.

`khdays_sound_mods_browser_stop()` cancels the pending decode timer and returns
queued entries to the retryable state. The browser timer leaves native exit
enabled and skips decode after an abort or terminal exit. Native host audio
runs the actual bridge through the cooperative pump in `../host/audio_emscripten.c`.
The memory-integration patch's independent wave-address conversion must also
be applied for that compact browser memory path; this sound patch does not
replace its mapping work.

## Apply and verify

```sh
git -C /path/to/khdays-port apply /path/to/web/native/sound/runtime-sound.patch
```

The test runner reads the pinned public commit into a temporary source tree,
applies this patch there and verifies pinned decoder headers. It does not
modify the caller's source checkout or copy generated game/ROM data. Supply
upstream's exact dependencies (also recorded in `MANIFEST.json`):

| Dependency | Commit |
| --- | --- |
| `nothings/stb` | `2c980bb59875b0d32144a71867fbdebb2f77cd20` |
| `mackron/dr_libs` | `dfe8377631000664666519fdb83da193fd8037f4` |
| Emscripten | `4.0.23` |

```sh
python3 web/native/sound/run_sound_test.py --upstream /path/to/khdays-port --stb /path/to/stb --dr-libs /path/to/dr_libs --emcc /path/to/emsdk/upstream/emscripten/emcc
```

The actual upstream `port/tests/sound_test.c` passes as Wasm with its full
sound library, real bridge/mods code and pinned Ogg/FLAC decoders: channel
sample golden hash, 10,000-reply FIFO ordering, volume/category behavior,
smoothing, stream reading, WAV/FLAC loop metadata, sample round trips and
resampling. Session logging is an explicit no-op fixture because that suite
does not test logging. The ARM7-binary driver and offline game-render cases are
**skipped without a ROM**, as upstream reports. They were not verified.

A separate regression exercises the actual browser replacement queue and
loader: input/PCM limits, a real synthetic WAV decode, priority/deduplication,
one-file callback turns, the queue cap and timer cancellation. Test data is
authored synthetic PCM only. Known upstream/decoder warnings remain visible;
no third-party source is silently patched. No game audio, ROM, extracted binary
or compiled output is included. This does not establish audible browser
playback, game performance, Windows regression behavior or a complete link.
