# Endless Heartless

Open the separate Combat Practice profile and select **Target preset → Endless
Heartless** in the native mod options. The existing default preset is unchanged.

Defeated targets refill while the remaining enemies stay in play. Each wave
contains two sets of its group, then advances automatically:

| Wave group | Maximum active enemies | Targets per wave |
| --- | ---: | ---: |
| Ground | 3 | 6 |
| Aerial | 2 | 4 |
| Armored | 1 | 2 |

The groups repeat without a final wave. A short native notice announces each
wave; **Encounter status** shows the wave, active targets and total targets
cleared in the current run. Replacements are attempted one at a time, at least
30 active frames apart. Existing targets are never replaced to fill another
slot, and no difficulty or actor-count escalation is applied.

**Extended combos** uses the selected HP multiplier for the entire current
wave, including its replacements. A durability change applies next wave.
**Between encounters** restores health between waves; **On restart only**
carries damage forward. Individual replacements never heal the player.

**Restart encounter** and defeat recovery begin a fresh run at wave 1 and
restore health and position. Pausing defers replacements and queued resets.
The Mixed-only flow and after-clear drill choices do not stop Endless.

The mode operates only inside the isolated practice arena. Persistent native
spawn refusals stop replacement attempts and show a manual restart action.
The Lua mock tests cover these rules; native combat, UI readability and browser
runtime integration require separate gameplay validation.
