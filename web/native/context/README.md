# Browser fiber context adapter

`context_emscripten.c` replaces upstream `port/host/app/context_win.c` and
`context_x64.asm`, using the same `context.h` API. It keeps custom game-window
C stacks and allocator ownership, and allocates a separate Asyncify continuation
stack for every context, including the original caller.

Build the ROM-free test with an Emscripten SDK:

```sh
emcc -std=c11 -O1 -Wall -Wextra -Werror -sASYNCIFY -sASSERTIONS=2 \
  -sSTACK_OVERFLOW_CHECK=2 -sALLOW_MEMORY_GROWTH=1 \
  -I/path/to/khdays-port/port/host/app \
  context_emscripten.c tests/context_smoke.c -o context-smoke.js
node context-smoke.js
```

The entire game, its indirect call graph, runtime and statically linked mod
must be built with compatible Asyncify instrumentation. Do not set
`ASYNCIFY_IGNORE_INDIRECT`; the game scheduler and plugin callbacks use indirect
calls. Do not share or migrate these contexts between browser Workers.

The smoke test verifies first entry, two nested switches, restoration of current
context and C locals across resume, actual local placement in the supplied C
stack, custom stack release ownership, deleting a
suspended context, rejected allocations and self-switching. It contains no ROM,
game assets or gameplay. The 1 MiB stack limits are initial configurable limits,
not measured production requirements.

Validated with the locally installed Emscripten SDK: the test compiled with
`-O1 -sASYNCIFY -sASSERTIONS=2 -sSTACK_OVERFLOW_CHECK=2` and executed successfully
under Node. This is a real WebAssembly/Asyncify test, not an API mock. It does
not establish game-sized continuation depth or scheduler behavior in gameplay.

Primary API reference: <https://emscripten.org/docs/api_reference/fiber.h.html>

This adapter alone does not port the game. Its memory layout, I/O/clock/audio,
static plugin registry and build/generated-code assumptions still need browser
adaptation and real game validation.
