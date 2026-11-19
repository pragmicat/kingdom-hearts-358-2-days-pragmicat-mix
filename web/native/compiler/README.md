# Days recompilation: Emscripten compiler step

This restores one necessary build step for **Kingdom Hearts 358/2 Days:
Pragmicat Mix**: the actual native Days reference-preparation pipeline can use
Clang/Emscripten on Linux with an explicit wasm32 C ABI profile. It uses the
recompilation's C source. No emulator, ROM, extracted content, artwork, saves,
generated game C or compiled game is included.

`upstream-compiler.patch` applies to unmodified `Yokimitsuro/khdays-port`
`v0.1.5`, commit `2037f79a171524eafcf40a1d641b16da14667a64`, with decomp submodule
`93535afd7ca082f3d856bbf1cadae65be16d44ba`. It changes only:

- `port/tools/prepare.py`: explicit preprocessor, compiler, worker and ABI options.
- `port/tools/references/preprocess.py`: Emscripten flags while retaining the
  existing MSVC argument path.
- `port/tools/references/build.py`: worker/backend plumbing, real Clang header
  dependency markers, version/profile-specific cache identity and Wasm checks.
- New `port/tools/references/wasm_abi.py`: audited supported wasm32 value classes.

The Windows defaults remain MSVC/windows-x64. The browser path requires the
tested Emscripten **4.0.23** driver and wasm32 profile. It does not reuse the
Windows rule that every 1/2/4/8-byte aggregate returns in a register: two-field
records can use a hidden destination in Wasm even at four/eight bytes. Direct
scalar return classes, exact argument classes and arity also participate in
cross-file checks. Unsupported by-value layouts stop with a named error.

Preprocessor output and parsed ABI metadata have separate cache identities.
Changing the ABI invalidates parsed shapes and emitted C while retaining
unchanged raw preprocessing. Header changes invalidate preprocessing too.

## Apply and verify

Use a developer-owned upstream checkout; this artifact does not modify the
Pragmicat source checkout automatically. Its patch applies independently of
the browser host and memory changes, which touch different source files.

```sh
git clone --depth 1 --branch v0.1.5 https://github.com/Yokimitsuro/khdays-port.git /tmp/days-recomp
git -C /tmp/days-recomp submodule update --init --depth 1 external/khdays-decomp
git -C /tmp/days-recomp apply /absolute/path/to/upstream-compiler.patch
python3 -m pip install pycparser==3.0
python3 /tmp/days-recomp/port/tools/references_test.py
python3 /tmp/days-recomp/port/tools/prepare_test.py
python3 tests/run_compiler_tests.py --upstream /tmp/days-recomp --emcc /path/to/emsdk/upstream/emscripten/emcc
python3 abi/tests/run_tests.py --upstream /tmp/days-recomp --emcc /path/to/emsdk/upstream/emscripten/emcc
```

Observed verification with Emscripten 4.0.23, Python 3.12 and Node 24:

- 31 existing rewrite cases and four existing source-preparation cases pass.
- Four compiler/cache contract cases pass, including actual changed-header
  invalidation and rejection of mixed target profiles.
- Eleven focused Wasm ABI cases pass; ten return classes match the installed
  compiler's actual LLVM IR.
- Two authored fixture translation units run through the real reference writer
  and execute as Wasm through emitted function records and typed route changes.
- One **original Days battle translation unit** runs through `rewrite_all`,
  including worker initialization, preprocessing, wasm32 shape analysis,
  indexing, emission and generated header. Its combo-window boundaries and
  actual generated hook replacement/restoration then execute as Wasm.

Generated C, LLVM IR and binaries are temporary and removed after each test.
The ABI lookup table used by its fixture is a test harness, not a restored
production static registry.

## Concrete next build task

Run full source preparation once after integrating this patch:

```sh
python3 /tmp/days-recomp/port/tools/prepare.py \
  --out /tmp/days-recomp/build/port/gen-browser \
  --preprocessor clang --abi-profile wasm32 \
  --compiler /path/to/emsdk/upstream/emscripten/emcc --jobs 8
```

The first full definition pass processed 24,425 translation units. It stopped
at 4,208 cross-file scalar or void return-class mismatches. Apply
`discarded-wasm-returns.patch` after `upstream-compiler.patch` to normalize
the definition's exact signature where a direct call discards the result.
The emitter rejects incompatible result uses and address-taking at the source
location. Indirect aggregate returns still require explicit agreement.

```sh
KHDAYS_TEST_EMCC=/path/to/emcc python3 /tmp/days-recomp/port/tools/references_wasm_returns_test.py
```

All ten focused tests pass, including real Emscripten linkage of five scalar
and void examples that fail to link before normalization. Two original Days
files with discarded returns can now emit. `entity-detach-void-contract.patch`
also corrects the original entity-detach routine's return type to match its
sole caller and the list-unlink operation. The four focused tests include
native Wasm unlinking from the head, middle and end of a list.
`forward-query-void-consumer.patch` corrects one invalid surface/sound wrapper.
It preserves the real enqueue argument and order before the sound update.
Five focused tests include executing the rewritten Wasm wrapper with varying
surface arguments.
`key-repeat-quotient-lowword.patch` uses the original unsigned divide helper's
64-bit result and explicitly extracts the quotient for the input repeat timer.
The real rewritten routine and HAL pass the Wasm regression at normal and
wraparound boundaries.
`pxi-init-void-contract.patch` also makes the original PXI initializer match
its callers and the FIFO initializer's void contract. The original wrapper
fails the Wasm return check. The repaired wrapper links and calls the provided
FIFO initializer once.
`irq-eight-void-veneers.patch` aligns the eight DMA and timer entry points with
the real interrupt table and `OSi_IrqCallback`. WebAssembly dispatch through
the original table type confirms the slot order, one-shot clearing, rearming,
interrupt flags and disable masks.
`sleep-alarm-void-contract.patch` gives the original sleep alarm handler its
one-argument, void callback signature. The original Wasm alarm queue dispatches
the handler with the sleeping thread's slot, clears both sleep references and
resumes that thread once. Run this focused check after applying the patch:

```sh
python3 tests/run_sleep_alarm.py --upstream /tmp/days-recomp --emcc /path/to/emcc
```

`card-transfer-contracts.patch` fixes two more original callers. The file system
passes its actual one-argument completion handler and archive to the native ROM
transfer. The backup lock still takes the caller's ID and backup target 2.
A focused Wasm link and dispatch check exercised the async request arguments,
both completion statuses and the lock arguments using fixture card services.

`allocator-free-contracts.patch` gives the expandable and unit heap free
wrappers the two-argument void contract used by the game's allocator tables.
The frame heap's individual free accepts the same two arguments and does
nothing. Both callbacks in each expandable/SDK pair have typed declarations
where the original table takes their addresses. One Wasm run dispatched
through both original tables, verified the original unit heap free-list push,
checked the expandable/SDK arguments and kept the frame heap intact. Emerald
Serenade's similarly labelled routine belongs to its AI state dispatch.

`vram-callback-contracts.patch` aligns the default texture and palette
allocators and the installed frame palette callbacks with the engine's
indirect calls. A focused Wasm dispatch check exercised the default table,
allocation from both ends of the frame palette, release, and rejection of
an allocation larger than the remaining space.

`sdk-state-wrapper-contracts.patch` corrects the return types on the LCDC
graphics-bank setter, the sound-player channel-priority setter, and the
expandable and frame heap destructors. Nine pinned SDK routines now rewrite
and link together. Running them in WebAssembly exercised the bank shadow and
selected registers, the sound priority command, and removal of both heaps
from their owning lists.

`font-splitter-contract.patch` gives the title-screen font initializer the
actual signature of its UTF-16 character reader. The reader advances its
cursor by one code unit and returns that unit through the installed callback.

`signed-division-quotient.patch` restores the CodeWarrior signed division
helper's two results on wasm32. Callers that declare a 32-bit integer read the
quotient. Callers that declare the full 64-bit result also receive the remainder.
The mission-results digit routine ran against the real helper in WebAssembly.
Dividing the most negative 32-bit integer by -1 produces the ARM result.

The resumed whole-game run with the initial discarded-return repair emitted
21,869 files. It reported additional incompatible returns and callback
signatures before it could write `sources.cmake`.
Rerun with the exact source repairs above, using the existing preprocessing
cache. Gameplay testing still needs a complete rewrite, full link and cartridge
boot.

The upstream MIT notice is preserved in `UPSTREAM_LICENSE.txt`. The ABI model
was independently authored from the target's behavior and primary LLVM
implementation references pinned in its source comments; no LLVM source or
runtime binaries are bundled.
