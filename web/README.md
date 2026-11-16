# Days recompilation browser port

The target is the **actual native Days recompilation**, statically linked with
Pragmicat combat and its Lua practice host. The user explicitly requires this
architecture. A DS emulator is not a substitute. There is no playable browser
game in this source yet.

`rom-import.mjs` validates supported NA/EU revision-zero cartridge hashes and
extracts their original files into an Emscripten filesystem, entirely locally.
`native/` contains genuine, separately tested WebAssembly runtime components
and integration work. No ROM, BIOS, game assets, saves, generated game C or
compiled game is included.

```sh
node --test web/tests/*.test.mjs
# Export the honest status page while the full runtime is incomplete:
python3 web/build_site.py --output /path/to/site/dist
```

See [native port components and remaining gates](native/README.md) and the
[Endless Heartless guide](../practice/ENDLESS.md). The native practice mode is
implemented; its browser availability depends on running the recompilation and
actual mod in WebAssembly. Do not enable browser Campaign/Practice controls
until those paths genuinely execute. The current page makes that limit clear.
