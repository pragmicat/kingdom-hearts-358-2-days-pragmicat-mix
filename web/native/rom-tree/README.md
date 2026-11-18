# Browser cartridge reader integration test

This test compiles the Days v0.1.5 native cartridge reader, VFS, and mod
discovery to WebAssembly with Emscripten 4.0.23. Use the same SDL3 3.4.12
headers and Wasm static library as the browser build.

```sh
python3 run_rom_tree_test.py \
  --upstream /path/to/khdays-port \
  --importer /path/to/pragmicat-mix/web/rom-import.mjs \
  --stb /path/to/pinned-stb \
  --sdl-include /path/to/SDL/include \
  --sdl-archive /path/to/wasm/libSDL3.a \
  --emcc /path/to/emcc
```

The JavaScript driver builds a tiny synthetic cartridge in memory. The real
importer rejects its unknown hash. The test then uses the real header and
FNT/FAT parsers to stage the fixture in the browser's Emscripten filesystem.
Native startup skips an incomplete `.partial` extraction. The reader opens
the extracted cartridge, resolves nested and unnamed files, reads raw data,
and decompresses an LZ11 file. Native mod ordering selects the enabled
override and updates its FAT entry to the relocated bytes.

The fixture contains no game code or assets. It exercises the extracted data
boundary and cartridge reader without starting the game or changing the
importer's authenticity check.
