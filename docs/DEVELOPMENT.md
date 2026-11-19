# Development handoff

This repository is **Kingdom Hearts 358/2 Days: Pragmicat Mix**, separate from the
Chain of Memories project. Use the current Pragmicat project identity throughout.
Source version is **0.3.3**, targeting Yokimitsuro's native Days PC port **0.1.5**.
This source review branch omits artwork. Windows packaging needs its original
artwork assets integrated, followed by a fresh build and validation.

## Current player-facing additions

**Latest priority: browser play from a local ROM using the Days recompilation,
with Pragmicat combat and Endless Heartless.** The hosted page is a status
preview while game linking and boot are pending. ROM selection and extraction
stay on the player's device. Cloud file-picker permission was declined, so
the import flow has Node coverage and still needs validation with a real cartridge.

The latest gameplay pass carries the aimed enemy through a multi-target launcher
or aerial sweep, and a fresh Technique tap survives the handoff between a
committed Light and its next swing. Endless reinforcements pick an open arena
pad if their usual one is crowded. Clearing a wave announces the next group
during the break. Drive uses directional marks when a form change is pending.
The Wasm mod publishes that same native HUD to Canvas between the two screens.
Browser gamepad attacks carry their input source through the combo buffer.
Confirmed melee damage can trigger one optional pulse per swing. Practice cues
come from the port's Lua notice queue and appear below the canvas.
Native controller feel and in-game readability still need a playthrough.

**Endless Heartless is implemented in the native practice source.** Select
Target preset 5 to refill individual defeated enemies while survivors remain,
then rotate bounded ground/aerial/armored waves without a final wave. Wave and
cleared counts, locked per-wave durability, healing at wave boundaries, pause,
defeat and finite failure recovery pass all **39 Lua groups**. A refused
replacement rotates the next attempt to another open slot, and unavailable
kinds are skipped during that refill check. The new regression failed before
the fix. First-run browser Practice selects this preset by default.

`web/native/` preserves pinned source patches for Clang/Wasm reference
generation, the static registries, actual DS arena memory and original game
startup. WebAssembly tests cover production memory, CPU rendering and mixer,
native analog movement, the cartridge reader, the nine combat modules, Lua
Practice loading, and save rollback on a failed storage write. The browser
launcher in `web/native/boot.mjs` waits for the first native frame. The full
compiler pass reached the cross-file return ABI check after preprocessing all
24,425 units. A verified compiler patch now repairs direct calls that discard
the result. Callers that read an incompatible value still need explicit
repairs before the full link and a real ROM boot. See
[`web/native/README.md`](../web/native/README.md) for the pinned checkout and
build sequence. Keep generated game objects and local cartridge data out of the
repository.

- **Technique direction feel:** native Precise / Standard / Forgiving choices
  use 30/45/60-degree toward/away half-cones. Standard retains the previous
  boundaries. Side and neutral moves remain available, and direction still locks
  at the button press. Damage, animation speed and movement are unchanged.
- **Longer combo drills:** Practice's Extended combos option uses three times
  each preset's normal target HP. It applies at the next spawn without replacing
  live enemies; actor counts, kinds and attack strength are unchanged.
- **Defense drills:** Practice's On restart only HP policy carries damage across
  automatic next/repeat encounters. First entry, Restart, Next mixed group and
  defeat recovery still heal. These settings stay in the isolated practice profile.
- **Drive gain feedback:** a 300 ms pale accent shows the latest added charge in
  the existing gauge, without flashing the panel. Pause/hidden views, form changes
  and resource resets clear it. See the updated renderer preview in UI_REVIEW.md.

Combined validation passes **194 portable C++ cases** and **33 practice Lua
groups**. The new choices and presentation need native controller/UI review. The offline
HUD preview uses production rendering code and is not a gameplay capture.

The Drive-hook recovery follow-up passes **17 Drive cases**. Initialization
resolves the original party-unload routine before installing its hook. Missing
or refused hooks leave native dispatch intact; an installation with a missing
predecessor restores it or remains a callable pass-through until stop can retry.
Hook ownership is tracked separately from successful initialization. Both new
failure-injection regressions failed before the fix. These mock-host checks do
not establish Windows/native lifecycle behavior; native validation remains open.

The Drive-setting follow-up passes **66 controller cases**. Charge connections
now matches every selectable count from 10 through 80 exactly: capacity scales
with the chosen count instead of truncating 1000/count credit per connection.
The default stays 1000 capacity, 25 credit and 40 connections; target dedupe and
the two-target-per-attack limit are unchanged. The parameter-path regression
failed before the fix and covers the full range plus clamped bounds. Native
custom-setting behavior and gauge presentation still need gameplay validation.

The charge-raster follow-up passes **11 HUD cases**. Pixel fill now reserves
empty/full silhouettes for zero/full fractions: partial charge keeps an empty
edge, while positive charge and the final active tick retain visible fill at
small window sizes. The Windows repaint key uses the same bounded calculation.
The pixel regression failed before the fix and covers multiple sizes/DPI values.
The offscreen renderer preview was inspected; native overlay composition and
contrast against actual battle backgrounds remain unverified.

The practice-completion pass adds one native notice when a cleared drill enters
**Wait for Restart**, naming the planned next group and the restart action. The
same message remains available from Encounter status. Automatic encounters and
manual resets stay quiet; pause/readiness guards defer the cue, and waiting does
not repeat it. All **29 practice Lua groups** pass, covering every preset, repeat
flow, pause, re-entry and scope release. Native notice placement/readability still
needs an in-game capture; C++ combat behavior is unchanged by this pass.

The aerial-input-generation follow-up passes **65 controller cases**. Accepted
pursuit/Enemy Step records its original Jump deadline and generation. Execution
consumes that request rather than a newer Jump or its intervening Light follow-up;
older attacks still retire, and simultaneous Light stays buffered. Ordinary jump
exits likewise capture their consumed request before calling native code. The
newer-input regression failed before the fix. Native multi-tap aerial chaining
and resulting animation connections still need controller validation.

The Drive-resume follow-up passes **15 Drive cases**. The timer now excludes an
interval beginning in pause or a cinematic, including a long gap before the
resume callback. Previously that callback could consume the entire remaining
form duration. Active combat/recovery still spends time, duplicate samples do
not, and expiry still waits for safe restoration. The new regression failed
before the fix; the existing pause case now protects the resume boundary too.
Configured duration is unchanged. Native pause/cinematic playback needs review.

The magic-transition follow-up passes **63 controller cases** and **9 combat-rule
cases**. Spell cancellation rechecks actor generation and attack identity after
the native request, preserving a replacement action's combo, hit policy and
buffered input. Ordinary accepted casts still save the route and retire outgoing
melee intent; native spell consumption and return values are preserved. Both new
regressions failed before the fix. Native melee-to-magic connections and spell
selection on a controller remain unverified and should be captured next.

The forced-signature follow-up passes **62 controller flow cases**. A forced
native state now cancels a signature accepted during the current combo step,
before native cleanup returns and starts it. Rush, Burst and Flurry are covered
across forced states 13/16/20/31; normal melee states 14/15/22 still preserve the
pending signature. The interruption regression failed before the fix. Native
damage/forced-action playback remains unverified and should be captured next.

The crowd-contact follow-up passes **30 motion cases**. Pursuit, Enemy Step and
launcher-rise confirmation now use the last hit on their chosen target, rather
than the latest hit anywhere in a sweep. Unrelated crowd contacts cannot revive
an expired mobility opportunity. Fresh hits on that target still renew it;
all damage/observer credit and accepted movement deadlines remain intact. Two
new regressions failed before the fix, with a third protecting queued movement
at its deadline. Native crowd combat remains a gameplay validation priority.

The aerial-transition follow-up passes **27 motion cases** and **60 controller
flow cases**. Pursuit and Enemy Step recheck the attack/actor identity after
native callbacks before applying impulses or consuming movement. Interrupted
transitions preserve replacement velocity and newer Enemy Step requests; the
controller also avoids clearing the replacement combo and input. Four new
regressions failed before their fixes. These are mock-host checks; actual
controller feel and native transitions still need gameplay validation.

Practice now offers **After clearing → Wait for Restart**. The optional break
leaves the empty arena, health and position untouched until a manual reset;
automatic encounters remain the default. Mixed groups advance once on clear,
and Next starts the planned group without skipping twice. Repeat drills still
support an explicit Next. Returning to automatic mode uses the normal delay.
All **27 practice Lua groups** pass, including manual/automatic resume, pause,
fixed presets and scope release. Native gameplay remains unverified.

The practice flow pass adds a Mixed-preset repeat mode and a manual next-group
action. Neither interrupts play when toggling repeat; skipping queues the
existing guarded reset and preserves fixed presets and field ownership.
All 24 practice Lua groups pass, including repetition, resumed progression,
paused/manual skips, fixed presets and out-of-scope actors. That practice-only
change left C++ untouched; later controller follow-ups are recorded below.

The target-retirement follow-up passes **59 controller flow cases**. A captured
target's native actor/part cache is now cleared even if its SDK handle has
retired before attack selection. Queued and committed follow-ups still advance
the combo; inputs without captured targets preserve native targeting. The new
regression failed before the fix. Native gameplay remains unverified.

## Aim of each pass

The signature-intent follow-up passes all **58 controller flow cases**. A fresh
Technique press while holding Light still requests a signature, as does a
deliberate simultaneous chord. Pressing Light while Technique remains held now
continues normal ground/air pressure rather than accidentally requesting a
signature. The regression failed before the change. Native feel remains unrun.

Improve responsive, fluid combat with clear inputs, connected native animations,
readable effects, and satisfying hit-confirmed movement. Preserve the distinct
Light, directional Technique, chosen signature, pursuit, Enemy Step, magic, and
Drive actions. Favor concrete combat feel and polish over unrelated features.
Read the latest source and this handoff, select one observable issue, implement
the change, and record its regression evidence and remaining native checks.

## Current regression work

- Forced control loss clears queued and accepted combat intentions; ordinary
  pause preserves accepted commands, while explicit menus cancel them.
- Enemy Step spending survives suspension until a real landing. Suspension drops
  queued movement and old hit confirmations without granting another air action.
- Accepted pursuit and Enemy Step queues retain their own bounded lifetime after
  the original Jump tap expires, execute once, and respect native exit gates.
- Frame-event sampling captures brief Light, Technique, signature, Jump, and
  defense taps between native actor callbacks. Frame stamps prevent duplicate
  sampling; expiry, pause, and control gates remain covered by regressions.
- Missing setup markers recover only after the copied extraction matches the
  source inventory and file hashes. Mismatches preserve data, saves, and runtime.

The 15 November 2026 source pass passed all **150 portable C++ cases** (49 flow,
17 limit, 19 motion, 8 rules, 14 Drive, 13 animation, 6 HUD, 13 practice boot,
11 weapon animation) and **15 practice Lua groups**. New input and motion
regressions failed before their fixes. Source whitespace and Python compilation
checks also passed. These results do not certify a Windows release or game feel.

The signature-input follow-up now passes **152 C++ cases** (51 flow) and the
same **15 Lua groups**. Both new regressions failed before the fix. Ordinary
and signature input now share the enabled/suppression gate: a script-suppressed
Roxas discards old intentions and refuses fresh taps even during a native
signature input lock. Rush, LightBurst and Flurry are covered through frame,
sample, translate and step entry points; fresh visible defense remains allowed.
Native gameplay and Windows validation remain outstanding.

The broader experience pass now passes **164 C++ cases** (57 flow, 24 motion,
7 HUD; other suites unchanged) and **18 practice Lua groups**. Defense retains
newer follow-ups while retiring replaced inputs and old aerial requests. Native
attacks retain hit credit across suspension without duplicate resource credit;
interrupted/replaced attacks cannot receive stale callback credit. Practice
resets recover from temporary/partial removal refusals and provide manual retry
after persistent failure. Drive displays charge percentage, and the control
guide separates grounded launcher/pursuit from airborne Enemy Step. New
behavioral regressions failed before their fixes. See
[the experience review](POLISH_REVIEW.md) for remaining measured polish work.

## Reproduce validation

The visible UI pass now passes **170 C++ cases** (10 HUD, 3 launcher layout)
and **21 practice Lua groups**. The HUD uses a compact angular pixel renderer,
authored glyphs, segmented charge and a steady low-time cue. Practice has short
on-demand controls and encounter status. The launcher reflows, fits its work
area, handles per-monitor DPI and exposes scrollable/copyable status with
explicit keyboard defaults. See [UI review and renderer preview](UI_REVIEW.md).
Actual Windows launcher/overlay interaction and native gameplay remain unrun.

Run from the repository root. Portable checks require Python and a C++20 compiler
available as `c++` or selected through `CXX`:

```sh
python3 tests/run_tests.py
python3 -m pip install lupa==2.8
python3 tests/practice_lua_tests.py
git diff --check
```

For focused development, `python3 tests/run_tests.py --only flow` selects one
suite. The portable runner executes the C++ modules with a mock host; Lua checks
use simulated practice interfaces. Neither establishes actual game behavior.

Windows x64 requires Python 3.13, Visual Studio C++ tools, CMake, and a Windows SDK:

```text
python build.py
python tests/run_tests.py
python -m pip install lupa==2.8
python tests/practice_lua_tests.py
python launcher/build_launcher.py
python launcher/check_launcher.py
python tests/verify_release.py
python package.py
```

The portable workflow runs combat checks and links the complete plugin with
read-only repository contents permission. It does not publish releases. The
Windows commands above require the original packaging artwork assets.
Optional native-engine regression usage is documented by
`python tests/native_combat_smoke.py --help`; it needs prepared game data and the
built mod. Native scripts and DLL loading still do not establish controller feel.

## Next priorities

Capture actual native gameplay with a controller: input-to-action response,
animation connections, hit timing, camera behavior, pursuit, Enemy Step, and
readable attack and magic VFX. Review campaign encounters, progression, enemy
resistance, crowd pressure, and damage balance as well as the practice arena.
Confirm Windows setup repair and packaged launcher behavior in their real host.
The launcher currently has no GitHub self-updater; this remains a secondary gap
after combat and playable-preview validation.
