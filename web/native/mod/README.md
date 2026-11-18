# Built-in Pragmicat mod and browser profiles

The Days PC port 0.1.5 already supports built-in plugins by mod ID. Include
`PragmicatBrowser.cmake` and call `pragmicat_add_browser_mod(game_target)` on the
Emscripten game target. Use its plugin table in place of the platform's generated
`static_plugins.c`. The mod compiles from the same source list as the PC build.
The link embeds the authored combat manifest and parameters in both profiles,
and adds the existing Lua script and mission definition to Practice.

Create a fresh runtime with `noInitialRun: true` and export `FS`. Inspect the
local cartridge and finish `importRomToFileSystem` before entering the game.
Call `prepareBrowserProfile(module, inspection.supported, mode)` from
`profile.mjs` before the native entry point. It hydrates the selected save
folder, seeds settings only where files are absent, then selects the native
practice flag and working directory. The native runtime should use relative
`saves` as its user folder. The returned `flushSaves()` shares a queue with
the port's native save callback. The callback reports success after IDBFS
commits the new file. If the commit fails, it restores the previous in-memory
file and tells the game the save failed.

Campaign and Practice keep separate storage for each supported cartridge. A
new Practice profile selects **Endless Heartless** through the real Lua host's
options file. Returning players keep the settings they chose.

## Verification

```sh
emcmake cmake -S web/native/mod/tests -B build/browser-mod-smoke \
  -DCMAKE_BUILD_TYPE=Release -DKHDAYS_PORT=/path/to/khdays-port
cmake --build build/browser-mod-smoke
node web/native/mod/tests/smoke.mjs build/browser-mod-smoke/pragmicat_mod_smoke.mjs
```

The smoke test links all production combat modules into WebAssembly. It calls
the built-in descriptor and the native mode setter, checks the embedded files
and save symlinks, and uses a stand-in for IndexedDB hydration. Browser storage
and gameplay need testing through the full recompilation.
