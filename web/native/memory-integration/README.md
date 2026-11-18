# Browser memory for the Days recompilation

Apply `runtime-memory.patch` to upstream `khdays-port` v0.1.5
(`2037f79a171524eafcf40a1d641b16da14667a64`) and define
`KHDAYS_BROWSER_MEMORY=1` for all game and runtime translation units. Use the
Asyncify context adapter for browser builds.

```sh
git -C /path/to/khdays-port apply --check /absolute/path/to/runtime-memory.patch
git -C /path/to/khdays-port apply /absolute/path/to/runtime-memory.patch
python3 web/native/memory-integration/run_test.py \
    --upstream /path/to/khdays-port \
    --context web/native/context/context_emscripten.c \
    --emcc /path/to/emscripten/emcc
```

The browser branch of the production `memory.c` allocates the runtime's DS
ranges rather than reserving a 2 GiB native virtual window. Main RAM mirrors
share one backing store, DTCM has independent storage, and `plain()` keeps
upstream's 64 KiB rounding. Scalar lookups use a page table, and checked span
lookups require the entire access to fit one mapping. The existing DS heap
creation path keeps both game heaps in main RAM.

The patch connects the runtime and generated game pointer macros, SDK
`kh.engine` pointer conversion, Lua memory reachability and ARM7 sound access
to this map. Native game stack addresses stay in a DS reference range across
Asyncify yields. Returned stack mappings stop accepting lookups until a new
context reuses them. The Windows virtual window remains the default build
when the browser macro is absent.

The regression compiles the patched production memory, window, game heap,
SDK engine and Lua memory bridge units with the real Asyncify adapter.
It checks mirror identity and canonical references, independent DTCM,
checked spans and expanded-heap rejection, the generated-C pointer header,
DS heap selection, runtime memory API reads and writes, ARM7 reads, stack
suspension and reuse, and unmap/remap. Diagnostic sinks, unused
dispatch tables and the Nitro heap entry points use small test fixtures.
It passed in Wasm under Emscripten 4.0.23 and Node 24.22.3.

The full runtime still needs the browser build and explicit function/hook
registries. The ROM-free memory test does not exercise COFF dispatch, boot
or play the game. Native Windows fault handlers and diagnostic units need
browser replacements in the host target.

A raw C pointer returned by `KHDAYS_W` checks its starting address. Callers
handling a full byte span can use `khdays_window_range`. Preserve a DS
reference for arithmetic that crosses a mirror boundary. A host pointer
through an alias reverses to the canonical address, and existing callers
that rely on a particular mirror identity need game validation. Wasm stack
overflow checks protect the context stack; the DS guard remains a lookup
boundary. The empty GBA ranges share the native FF-initialized values, with
no Wasm page protection. DS-sized heap headroom still needs real gameplay
measurement with the mod loaded.

The bundle contains authored source changes and test fixtures. It does not
carry a ROM, extracted assets, generated game C, saves or a game binary.
