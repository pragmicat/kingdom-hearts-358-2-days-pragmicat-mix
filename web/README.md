# Days recompilation browser port

The target is the **actual native Days recompilation**, statically linked with
Pragmicat combat and its Lua practice host. The user explicitly requires this
architecture. A DS emulator is not a substitute. There is no playable browser
game in this source yet.

The browser importer accepts a raw `.nds` or a ZIP containing one `.nds`. It
reads only that cartridge from a stored or deflated single-volume archive.
Other files can stay in the ZIP. Decompression, the archive checksum, and
the supported NA/EU revision-zero cartridge hash checks run on the player's
device. The verified cartridge supplies the original files to Emscripten.
The import progress callback reports `unzip`, `verify`, then `extract`.
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
