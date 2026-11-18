# Native browser profile settings

`PragmicatBrowserServices.cmake` adds the original Days PC port 0.1.5 settings,
HUD settings and localization units with the existing Wasm SDL3 target.

The selected working directory and its `saves` symlink must be ready before native entry. The browser instance service supplies the user folder and slot. The original firmware initialization then reads the selected profile's language and nickname. The offline host declines wireless startup before the server profile value is consulted.

Apply `runtime-browser-profile-defaults.patch` before wiring profile values into browser startup. Practice uses autosave off when a value is absent, invalid, or the profile cannot be parsed. Valid explicit choices remain usable. Campaign keeps the original file 99 default.

`khdays_host_start` applies the active profile after the Canvas readiness check
and before native firmware boot. The profile's INI file stays local.

The existing browser sound setter forwards the five gains to the native sound mixer. Playback permission still belongs to the browser's explicit audio gesture. The save-list setter applies the file count and sets the native autosave mode together. The settings read does not select or modify either mode's saved progress.

Run the regression against the patched upstream source, the browser native adapter directory, and the pinned SDL 3.4.12 headers and Wasm archive. The runner requires Emscripten 4.0.23 and Node.

```sh
python run_test.py --upstream /path/to/patched-days-port \
  --sdl /path/to/SDL --sdl-wasm-archive /path/to/libSDL3.a \
  --browser-native /path/to/pragmicat/web/native
```

The test runs the real parser, localization, HUD settings, browser mode
selector and instance service in WebAssembly. It uses a virtual filesystem
with distinct Campaign and Practice profiles. It verifies the startup
helper's language, mixer and save-setting calls through recording sinks. It
also covers nickname, server preference, HUD values, and omitted or broken
settings. The separate sound and save tests exercise those native backends.
