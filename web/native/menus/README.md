# Browser runtime menus

Apply `runtime-browser-menus.patch` to pinned Days PC port 0.1.5 source with the
browser counter support in `port/runtime/core/browser_counter.h`. Compile
`camp.c`, `charselect.c`, `cinelist.c`, `daycard.c`, `enemy_viewer.c`,
`newgame.c` and `shop.c` for the browser target. These units use the single
cooperative host thread for menu snapshots and retain the original native
scene and update routes.

Character selection and day-card timing use the browser monotonic clock.
Day-card timing waits for the first HD drawing notification. This prevents
the browser's new clock epoch from accidentally enabling HD pacing for an
original DS calendar card at startup.

```sh
git -C /path/to/khdays-port apply /path/to/runtime-browser-menus.patch
python3 run_test.py --upstream /path/to/khdays-port
```

Emscripten 4.0.23 compiles the seven original menu units. Node runs the actual
day-card guard for missing, recent and old draw notifications. Tests compare
preprocessor tokens with the pinned Windows branches using an empty SDK
header fixture, and do not establish a desktop or browser game boot.
