# Browser texture and asset loading

Apply `runtime-assets.patch` to the pinned Days recompilation v0.1.5 checkout
(`2037f79a171524eafcf40a1d641b16da14667a64`). The browser target compiles
`port/runtime/mods/textures.c` and `sheet_colours.c` with `KHDAYS_BROWSER=1`.
Their texture stores, replacement versions, directory creation and palette
collection remain part of the real native runtime.

The single-threaded Wasm target serializes the game and software presentation
callbacks. Texture readers and writers therefore need no Windows thread locks
there. The registry rejects Wasm pthread builds until it has equivalent
synchronization. Desktop builds keep their original Windows lock and publish
operations. The texture and sheet output paths share the runtime's local
directory helper, using `mkdir` on Wasm.

The upstream portable `assets` CMake project can build its `khdays-assets`
archive on the same Emscripten toolchain. The native target can alternatively
compile the existing selected `assets/src` units directly. Keep exception
handling on for C++ decoder errors and link `khdays-assets` only once if using
the full archive.

```sh
git -C /path/to/khdays-port apply --check /absolute/path/to/runtime-assets.patch
git -C /path/to/khdays-port apply /absolute/path/to/runtime-assets.patch
python3 web/native/assets/run_asset_tests.py \
    --upstream /path/to/khdays-port \
    --stb /path/to/pinned/stb \
    --sdl-include /path/to/SDL/include \
    --sdl-archive /path/to/wasm/libSDL3.a \
    --emcc /path/to/emcc
```

The ROM-free Wasm checks compile the two patched graphics units and exercise
the production texture registry, ROM mod-path lookup, VFS and stb PNG codec.
They write and reload a test PNG from nested directories, verify that old
entries stay stable as the registry crosses a block boundary, and verify
versioned texel updates. They also run the upstream VFS, LZ message, NFTR
font and 2D graphics test programs. The runner uses Emscripten 4.0.23,
upstream SDL3 3.4.12, and the stock stb commit from the port's build
(`2c980bb59875b0d32144a71867fbdebb2f77cd20`).

These tests use temporary synthetic files. They do not establish real game
boot, rendering performance, or a chosen ROM's asset decoding in play.
