# Compact browser-memory translation experiment

This is a bounded, ROM-free prototype for one porting seam. It is **not wired
into the game**. Actual WebAssembly tests pass against the Emscripten fiber
adapter, including a game-stack local converted to a DS reference, modified
through that reference, suspended and resumed.

The map translates a 32-bit DS reference into caller-owned contiguous storage.
Every request states its full byte span. Canonical mappings reject overlapping
DS ranges and overlapping backing stores. Aliases immediately share storage;
reverse conversion returns a canonical reference. Removing storage is refused
while an alias still refers to it.

## Exact semantics and limits

- `W(REF(host)) == host` for every mapped canonical host address.
- `REF(W(ref)) == ref` for canonical references, including high heap addresses,
  game-stack locals and the 24-bit archive-header range.
- For a RAM mirror reference, `REF(W(alias))` is the corresponding canonical RAM
  reference. Equal host pointers cannot retain which DS alias produced them.
  This differs from the native virtual-address implementation and needs a game
  audit before integration.
- The tested mirror regions are `02400000..027dffff` and
  `027f0000..027fffff`, backed by the corresponding 4 MiB main RAM locations.
  DTCM `027e0000..027e3fff` has independent storage. The remainder of its native
  64 KiB allocation granule is deliberately unmapped.
- Cross-region spans, RAM mirror wraps, unmapped addresses, overflow and
  zero-length spans are rejected. A caller that translates a one-byte pointer
  and then performs unchecked arithmetic can still cross a boundary; this API
  cannot make such raw C operations safe. Existing bulk-copy and pointer-walk
  callers need full-span conversions or an explicit segmented-copy helper.
- Memory ownership, initialization, heap growth, zero-fill/reset semantics and
  allocation failure policy are deliberately outside this translator. The test
  maps small low/high heap fragments to verify offsets; it does **not** allocate
  or validate the native 1.5 GiB heap region. NitroSystem heap creation writes
  both ends of a free span, so mapping only touched pages is insufficient for
  arbitrary contiguous bulk accesses without an allocator/access audit.
- A possible initial boot route is upstream `KHDAYS_DS_MEMORY=1`: existing
  `game_heap.c` then creates both heaps in the original DS arena, avoiding the
  expanded 1.5 GiB region. `memory.c` makes `02800000..02ffffff` two full RAM
  aliases instead of header storage. The test verifies this alternate mapping
  too. Native addon/cache requirements and game behavior in this mode remain
  unvalidated; it is a potential porting route, not an established solution.
- This is a checked API rather than a protection-fault mechanism. It cannot
  reproduce Win32 no-access pages or the native fault-driven heap commitment.
  A Wasm out-of-bounds trap applies to the entire module memory, not individual
  logical DS regions.

## Upstream map inventory

From `khdays-port` v0.1.5 `port/runtime/core/memory.c`, `window.h`,
`enemy_overlays.h`, `io.h` and the generated-data definitions:

| DS range | Native purpose | Browser requirement |
| --- | --- | --- |
| `01ff8000..01ffffff` | ITCM | Dedicated 32 KiB storage |
| `02000000..023fffff` | Main RAM | Dedicated 4 MiB storage |
| `02400000..027dffff`, `027f0000..027fffff` | Main RAM mirrors | Canonical alias translation; audit reverse-reference normalization |
| `027e0000..027e3fff` | DTCM | Dedicated 16 KiB; exclude mirror granule |
| `02800000..02ffffff` | Header heaps, or RAM mirrors in DS-memory mode | Dedicated header storage normally; alternate-mode aliases must be selected explicitly |
| `03000000..03007fff` | Shared WRAM | Dedicated 32 KiB |
| `037f8000..0380ffff` | ARM7 WRAM | Dedicated storage; native rounds to allocation granules |
| `04000000` region | DS I/O/card/IPC | Keep translated register model; remove native fault-handler assumptions |
| `05000000..050007ff` | Palettes | Dedicated storage |
| `06000000..068a3fff` | VRAM | Contiguous VRAM storage |
| `07000000..070007ff` | OAM | Dedicated storage |
| `08000000`, `0a000000` granules | Empty GBA slot | Shared FF-filled storage; logical read-only checks |
| `09000000..09bbffff` | Enemy overlays 114–301 | 188 × 64 KiB storage |
| `09c00000..09c0ffff` | Enemy-class handler/slot tables | Dedicated 64 KiB storage |
| `0b100000` onward | Register model | Generated I/O view and second view, 64 KiB each |
| `0b200000` onward | Rewritten static-data area | Use generated `KHDAYS_DATA_AREA_END`; do not guess size |
| `0c000000..1bffffff` | Context C stacks | Register each stack's DS span; leave guard gaps unmapped |
| `20000000..2fffffff` | Expanded main heap | 256 MiB logical span; contiguous-access/allocator audit |
| `30000000..7fffffff` | Expanded sub heap | 1.25 GiB logical span; contiguous-access/allocator audit |

Native `plain()` rounds ordinary regions to 64 KiB. Logical sizes above retain
the source's requested extents; an actual integration must deliberately choose
which native padding behavior matters.

## Explicit base-address assumptions needing replacement

An inventory of direct arithmetic found in production sources (in addition to
every generated `KHDAYS_W`/`khdays_host`):

| File | Assumption |
| --- | --- |
| `port/runtime/core/window.h` | `KHDAYS_W` is `khdays_window + reference` |
| `port/compat/khdays_references.h` | Rewritten C uses the same macro and `khdays_host` adds the base directly |
| `port/runtime/core/window.c` | `khdays_reference` subtracts one base and accepts a 4 GiB offset |
| `port/runtime/core/memory.c` | All mappings, returned stack offsets and fault-driven heap commitment use one base |
| `port/runtime/plugins/engine.c` | SDK address/reference conversion subtracts the base and validates contiguous memory ranges |
| `port/runtime/core/io_guard.c` | Native address-fault classification tests/subtracts the base |
| `port/runtime/core/diag.c` | Host-PC-to-DS diagnostics and bad-read probing depend on a contiguous protected window |
| `port/runtime/mods/models.cpp` | Stack-local membership check subtracts the base |
| `port/runtime/sound/sound_bridge.c` | One sound-command path derives its DS address by base subtraction |
| `port/sound/snd7_driver.c`, `snd7_internal.h` | ARM7 sound access stores `snd7_ds_base` and adds each DS address |

The checked translator does not replace these callers automatically. Native
tests also contain direct arithmetic and must use an explicit test adapter if
they are reused. Bulk memory operations on pointers returned by `KHDAYS_W`
need their own audit even when they contain no direct base arithmetic.

## Reproduce the real Wasm test

```sh
emcc -std=c11 -O2 -Wall -Wextra -Werror -sASYNCIFY -sASSERTIONS=2 \
  -sSTACK_OVERFLOW_CHECK=2 -sALLOW_MEMORY_GROWTH=1 \
  -I. -I/path/to/khdays-port/port/host/app \
  compact_window.c /path/to/browser-fiber-context/context_emscripten.c \
  tests/window_smoke.c -o window-smoke.js
node window-smoke.js
```

Executed successfully with Emscripten 4.0.23 and Node 24.22.1. The test uses no
ROM or game assets and is not evidence of game runtime or browser performance.
