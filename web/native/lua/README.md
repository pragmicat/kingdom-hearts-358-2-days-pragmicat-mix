# Lua sandbox for the browser recompilation

`PragmicatBrowserLua.cmake` adds the real Lua 5.4.9 library to the
Emscripten game target. It uses the source URL and SHA-256 pinned by the
Days recompilation v0.1.5 and selects the same 28 interpreter/library units.
The game target supplies its SDL3 dependency and the upstream
`port/runtime/scripts` sources from the native source list. Keep Lua's MIT
notice with future linked distributions.

```cmake
include(path/to/PragmicatBrowserLua.cmake)
pragmicat_add_browser_lua(khdays-browser)
```

The standalone test compiles the upstream `khdays-script-test` source list
with Emscripten 4.0.23 and a Wasm SDL3 3.4.12 static build. The runner verifies
the Lua archive hash before extracting it. With `--practice` it also loads
the project’s actual practice manifest and Lua script through the production
sandbox in a temporary filesystem. The test checks that the browser's seed
`targets = 5` selects the registered **Endless Heartless** choice and that the
script binds its events.

```sh
curl -fL https://www.lua.org/ftp/lua-5.4.9.tar.gz -o lua-5.4.9.tar.gz
python3 web/native/lua/run_script_tests.py \
    --upstream /path/to/khdays-port-v0.1.5 \
    --lua-archive lua-5.4.9.tar.gz \
    --sdl-include /path/to/SDL/include \
    --sdl-archive /path/to/wasm/libSDL3.a \
    --emcc /path/to/emcc \
    --practice practice
```

The 37 upstream test groups and the Pragmicat practice loader test passed in
actual Wasm under Node v24.22.3. The CMake helper independently linked and
ran a Lua smoke program. The five production bridge units excluded from the
upstream test target also compiled as Wasm objects: `game_link.c`,
`deck_link.c`, `time_link.c`, `enemies_link.c`, and `enemy_names.c`. Their
compile check used the memory patch, the prepared DS symbol table, and a
bounded data-area header fixture. The real game target uses the complete
generated headers. The tests do not run the game or fight live waves.
