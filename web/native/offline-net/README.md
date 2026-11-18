# Offline browser networking

Apply `runtime-browser-offline-net.patch` to Days PC port 0.1.5 (`2037f79a171524eafcf40a1d641b16da14667a64`) after adding the browser runtime clock helper (`port/runtime/core/browser_counter.h`). Select `port/host/net/net_browser.c` as the browser's network host and retain the original `runtime/net` sources. Build with `KHDAYS_BROWSER=1` on a single cooperative thread. The desktop build keeps Winsock. The browser opens no sockets and starts no networking worker. The native wireless layer reports its existing `WM_ERRCODE_WM_DISABLE` when requested.

Native title boot and Pragmicat Practice both store offline session mode `1` before creating the native game service. In this mode the session sets its own member mask to `1`. Practice requests the field scene directly. This is a source audit of the offline path. Campaign and Practice still need an in-browser gameplay check.

With the browser session-log patch applied, run the verification using pinned Emscripten 4.0.23, SDL 3.4.12 WebAssembly static library, and Node.

```sh
python run_test.py --upstream /path/to/patched-days-port \
  --sdl-include /path/to/SDL/include \
  --sdl-wasm-archive /path/to/libSDL3.a
```

The test links the original network session and SHA-1 units with the browser transport, session log and SDL. It checks refusal before server settings are read, absence of native room and event state after repeated attempts, guarded outputs on unsupported host calls, and the platform clock and randomness. It compiles all five browser network objects and compares the desktop room list's preprocessed tokens with the pinned source.
