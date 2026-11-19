# Kingdom Hearts 358/2 Days: Pragmicat Mix

A combat overhaul and launcher built on [Yokimitsuro's native Days PC port **0.1.5**](https://github.com/Yokimitsuro/khdays-port/releases/tag/v0.1.5). Normal Roxas gets flowing Light chains, directional Techniques, selectable signature finishers, aerial pursuit, and a charged dual-wield Drive form.

Combat source and portable regression tests. Artwork integration and a newly branded Windows release still need to be completed and validated. Combat feel and encounter balance still need hands-on review. This source review branch does not contain the artwork required for Windows packaging.

### Play

A browser build of the **actual Days recompilation** is in progress, including
Pragmicat combat and Endless Heartless. It is not playable in the browser yet.
The [private browser preview](https://days-pragmicat-browser.keensaffron16.chatgpt.site)
reports that status; [browser source](web/README.md) contains the local-ROM
importer and verified WebAssembly port components. No game content is included.

A new Pragmicat Windows release is not available yet. Build from this source using the instructions below. Once a validated release is published, download **Kingdom-Hearts-358-2-Days-Pragmicat-Mix.exe** from [Releases](https://github.com/pragmicat/kingdom-hearts-358-2-days-pragmicat-mix/releases) and open it. The launcher offers **Campaign** and **Combat Practice** for 358/2 Days, with the mod included.

First setup finds your existing Days PC port and prepared game data, or asks you to select `khdays-native.exe` once. It makes a separate playable copy. After setup, open Pragmicat Mix directly. Campaign imports a copy of your existing progress once; the original installation stays unchanged.

**358/2 Days Combat Practice** opens a fresh arena with ground, aerial, and armored targets. Enemies return between encounters, health recovers, and Drive starts ready. Choose targets or restart an encounter in **Settings → Mods → Kingdom Hearts 358/2 Days: Combat Practice**. Practice uses separate progress. All action families also work throughout the campaign without a special Keyblade or panel loadout.

| Action | Game input |
| --- | --- |
| Light | Attack / bottom face button |
| Technique | X / top face button, plus a direction |
| Signature finisher | Hold Light and press Technique |
| Launch and pursue | On the ground, Away + Technique; press Jump after the launcher connects and the enemy rises |
| Enemy Step | In the air, press Jump after a melee hit on a nearby airborne enemy |
| Dodge / guard | Normal defense button |
| Activate / end Drive | Select / Back when charged |

Directions are relative to the target. Signature directions select rush (toward), burst (away), or flurry (neutral/side). A signature requires a fresh Technique press while Light is held; pressing Light while Technique remains held continues the normal chain. Rebind the underlying buttons in the game's Settings. Mod options control speed, buffering, Drive duration, and its display.

**Technique direction feel** offers **Precise**, **Standard** and **Forgiving**.
Forgiving accepts wider diagonal inputs toward/away from your target for advancing
strikes, launchers and Rush/Burst signatures. Precise leaves wider left/right
regions for side Techniques. Standard keeps the original direction boundaries;
all three retain neutral and side commands. Direction is captured when you press.

Practice's Mods menu includes **Combat controls** and **Encounter status** for
on-demand help. With the Mixed preset, **Encounter flow** can repeat the current
group for focused drills; **Next mixed group** skips to the next ground, aerial
or armored group through a normal reset. Changes wait for play to resume and
leave fixed target presets unchanged. Set **After clearing** to **Wait for Restart**
to keep the arena empty between drills. A short completion notice names the next
group and reminds you to restart when ready. **Restart encounter** starts the planned
group; **Next mixed group** starts that group or advances a repeating drill.
Automatic encounters remain the default. See the [visible interface review](docs/UI_REVIEW.md) for the
new pixel HUD preview and launcher layout work.

For focused drills, use these practice options:

| Goal | Setting |
| --- | --- |
| Fight replenishing Heartless through repeating ground, aerial and armored waves | **Target preset → Endless Heartless** |
| Keep a target alive through longer ground or aerial chains | **Target durability → Extended combos (3x HP)** |
| Practice defense across several encounters without a health refill | **HP recovery → On restart only** |
| Repeat the same enemy group | **Encounter flow → Repeat current group** |
| Take a break after each clear | **After clearing → Wait for Restart** |

Durability applies to the next spawned group; it does not interrupt a current
combo. On restart only carries damage across automatic next/repeat encounters.
First entry, Restart, Next mixed group and defeat recovery still restore health.
These training settings affect the separate practice arena.

Endless Heartless refills individual defeated targets while survivors remain,
then rotates groups without a final wave. A clear announces the next group before
it arrives. Reinforcements use the arena's existing spawn pads and look for open
space around Roxas and surviving enemies. Encounter status tracks the wave and
total cleared. Restart begins a fresh run. The PC and browser recompilation use
the same [Endless practice script](practice/ENDLESS.md).

Light continues your normal ground or air chain. Technique changes with direction:

| Direction | Ground Technique | Air Technique |
| --- | --- | --- |
| Neutral | Wide sweep | Descending finisher |
| Toward target | Advancing strike | Aerial carry |
| Away from target | Launcher, then a carry if repeated | Descending finisher |
| Left | Pressure strike | Descending finisher |
| Right | Flurry finisher | Descending finisher |

Jump and defense can be tapped slightly early and wait for an eligible exit;
missed attacks retain their recovery. A clean hit opens earlier defensive exits.
One dodge or guard can retain your combo route for a brief follow-up. Once a
Light is accepted, you can tap the next Technique during the swing transition.
It keeps the direction and target from that press until the normal buffer expires.
When a launcher or an aerial sweep hits several Heartless, pursuit and Enemy Step
prefer the one you aimed at and damaged.

Magic uses the game's normal spell selection. After the melee contacts finish,
an eligible native spell request can flow out of the attack. Successful distinct
melee actions gradually restore charges of equipped offensive spells; Cure is
excluded. Drive shows charge as a percentage, then **READY** when available,
**QUEUED** while waiting to transform, and time remaining once active.
Newly gained Drive charge receives a brief pale accent on that part of the bar,
giving a small combat-feedback cue without flashing the full display. Gold
right-pointing marks mean your Drive request is queued. Silver left-pointing
marks mean dual wield is ending. On supported browser gamepads, a confirmed
melee hit gives one short vibration per swing. Ending attacks give a stronger
pulse. **Gamepad hit vibration (browser)** turns this feedback on or off.

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

Once the Windows packaging assets are available and validated, `dist/` contains the standalone launcher and an optional ZIP. GitHub Actions currently runs the portable combat checks. Game files and saves are imported locally and are never included in downloads. `src/` contains native combat and practice entry, and `practice/` contains the arena.

On Linux or macOS with a C++20 compiler, `python3 tests/run_tests.py` runs the portable mock-host suites. These checks cover combat logic, not Windows UI or actual gameplay. See [development progress and next priorities](docs/DEVELOPMENT.md).

For optional real-engine combat checks, run `python tests/native_combat_smoke.py --help`. These require your prepared game data and use fresh test progress.

Original code is MIT licensed; the native SDK retains its own license. Native math test references credit [khdays-decomp](https://github.com/Yokimitsuro/khdays-decomp). Game names and artwork belong to their respective owners; see [artwork credits](assets/NOTICE.txt). This is an unofficial fan project.
