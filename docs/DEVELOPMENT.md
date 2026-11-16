# Development handoff

This repository is **Kingdom Hearts 358/2 Days: Pragmicat Mix**, separate from the
Chain of Memories project. Use the current Pragmicat project identity throughout.
Source version is **0.3.3**, targeting Yokimitsuro's native Days PC port **0.1.5**.
This source review branch omits artwork. Windows packaging needs its original
artwork assets integrated, followed by a fresh build and validation.

## Aim of each pass

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

## Reproduce validation

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
