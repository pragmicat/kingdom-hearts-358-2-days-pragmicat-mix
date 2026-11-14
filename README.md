<p align="center"><img src="assets/kingdom-hearts-358-2-days-pragmicat-mix-logo.png" width="620" alt="Kingdom Hearts 358/2 Days: Pragmicat Mix"></p>

# Kingdom Hearts 358/2 Days: Pragmicat Mix

A combat overhaul and launcher built on [Yokimitsuro's native Days PC port **0.1.5**](https://github.com/Yokimitsuro/khdays-port/releases/tag/v0.1.5). Normal Roxas gets flowing Light chains, directional Techniques, selectable signature finishers, aerial pursuit, and a charged dual-wield Drive form.

Source for a playable preview. Automated checks cover combat logic and native integration; a newly branded Windows release still needs to be built and validated. Combat feel and encounter balance still need hands-on review.

### Play

A new Pragmicat Windows release is not available yet. Build from this source using the instructions below. Once a validated release is published, download **Kingdom-Hearts-358-2-Days-Pragmicat-Mix.exe** from [Releases](https://github.com/pragmicat/kingdom-hearts-358-2-days-pragmicat-mix/releases) and open it. The launcher offers **Campaign** and **Combat Practice** for 358/2 Days, with the mod included.

First setup finds your existing Days PC port and prepared game data, or asks you to select `khdays-native.exe` once. It makes a separate playable copy. After setup, open Pragmicat Mix directly. Campaign imports a copy of your existing progress once; the original installation stays unchanged.

**358/2 Days Combat Practice** opens a fresh arena with ground, aerial, and armored targets. Enemies return between encounters, health recovers, and Drive starts ready. Choose targets or restart an encounter in **Settings → Mods → Kingdom Hearts 358/2 Days: Combat Practice**. Practice uses separate progress. All action families also work throughout the campaign without a special Keyblade or panel loadout.

| Action | Game input |
| --- | --- |
| Light | Attack / bottom face button |
| Technique | X / top face button, plus a direction |
| Signature finisher | Hold Light and press Technique |
| Launch / pursue / Enemy Step | Away + Technique, then Jump after a hit |
| Dodge / guard | Normal defense button |
| Activate / end Drive | Select / Back when charged |

Directions are relative to the target. Signature directions select rush (toward), burst (away), or flurry (neutral/side). Rebind the underlying buttons in the game's Settings. Mod options control speed, buffering, Drive duration, and its display.

### Build

Windows x64, Python 3.13, and Visual Studio C++ tools with CMake and a Windows SDK:

```text
python build.py
python tests/run_tests.py
python -m pip install lupa==2.8
python tests/practice_lua_tests.py
python launcher/build_launcher.py
python tests/verify_release.py
python package.py
```

`dist/` contains the standalone launcher and an optional ZIP. GitHub Actions runs the checks and attaches both to releases. Game files and saves are imported locally and are never included in downloads. `src/` contains native combat and practice entry, and `practice/` contains the arena.

For optional real-engine combat checks, run `python tests/native_combat_smoke.py --help`. These require your prepared game data and use fresh test progress.

Original code is MIT licensed; the native SDK retains its own license. Native math test references credit [khdays-decomp](https://github.com/Yokimitsuro/khdays-decomp). Game names and artwork belong to their respective owners; see [artwork credits](assets/NOTICE.txt). This is an unofficial fan project.
