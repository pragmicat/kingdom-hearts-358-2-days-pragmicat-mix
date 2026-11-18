# Browser save menus

Apply `runtime-save-menus.patch` to the pinned Days PC port 0.1.5 source.
Compile its `autosave.c`, `saveworld.c`, `savelist.c`, and `saveslots.c`
with the other browser runtime units. The cooperative browser host maintains
the menu state on a single thread. Desktop builds keep their Windows locks and
atomic mode settings. Original save signing, slots and reads/writes use the
existing `files.h` interface.

The browser uses native file 99 autosave by default for Campaign. The built-in
practice startup switches the browser default off through its profile's
`PRAGMICAT_PRACTICE=1` flag. An explicit native autosave mode takes precedence.

```sh
git -C /path/to/khdays-port apply /path/to/runtime-save-menus.patch
python3 run_test.py --upstream /path/to/khdays-port --mod /path/to/pragmicat-mix
```

The test compiles all four actual save units using Emscripten 4.0.23. It
runs browser mode initialization and the upstream save-slot tests with the
production browser file backend. The Node harness simulates IndexDB
synchronization success and one failure, which checks that a failed write
restores the old save bytes and that the native slot still reports failure.
Full browser game and IndexedDB behavior require a real runtime check.
