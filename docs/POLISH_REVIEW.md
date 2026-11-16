# Combat and experience review

## Improvements in this pass

| Player experience | Change | Evidence |
| --- | --- | --- |
| Dodge into a follow-up | Defense preserves newer attack/Jump taps, target and direction while consuming replaced inputs | Controller regressions reproduce lost attacks and stale Jump requests |
| Pause during an attack | The continuing attack can confirm fresh damage after resume without granting duplicate resource credit | Motion regressions check native damage, observer credit and forced interruption |
| Interrupted aerial transitions | Recheck actor/attack identity after native callbacks before applying pursuit/Enemy Step impulses or clearing combo input | Regressions reproduce stale velocity writes and lost replacement requests; ordinary native transitions remain covered |
| Predictable crowd mobility | Keep pursuit/Enemy Step freshness tied to the chosen target's hit rather than later contacts on other enemies | Two regressions reproduce unrelated-hit timer renewal; accepted requests keep their original target and deadline |
| Reliable practice reset | Verify target removal, retry temporary refusals, then reset position and spawn once; offer manual retry after a persistent refusal | Practice regressions cover partial removal, retry, pause and changed field |
| Readable Drive progress | Show charge percentage before READY, then existing queued/active states | HUD model checks rounding, full charge and large capacities |
| Understandable commands | Separate grounded launcher/pursuit from airborne Enemy Step and document directional Techniques and normal magic selection | Control guide matches the current route and spell adapters |
| Adjustable directional input | Native Precise / Standard / Forgiving choice changes toward/away cones while keeping neutral and side commands | Controller cases cover all profiles, default boundaries and captured direction |
| Longer focused drills | Optional Extended combos targets use 3x preset HP without changing enemy count or strength | Lua cases cover every preset, next-spawn application and live status |
| Defense practice across encounters | Optional On restart only HP recovery carries damage between waves, with manual/defeat recovery | Lua cases cover normal progression, repeat, pause, restart and scope |
| Visible resource gain | A brief pale band marks newly added Drive charge in the existing gauge | Portable feedback and raster cases, plus an inspected offscreen preview |

These changes have executable code/fixture checks. Native controller feel,
Windows display and actual combat footage still need validation.

## Next priorities

The items below are measured validation targets or feature candidates, rather
than claims that a defect has already been observed in the game.

| Priority | Area | What a successful pass should establish |
| --- | --- | --- |
| 1 | Controller response | Short taps reliably reach one intended action through impact pauses, defense recovery and form changes at every supported speed |
| 1 | Animation and contact timing | Ground/air chains, finishers and cancels remain connected without visible pose snaps or misleading hit timing |
| 1 | VFX and camera readability | Enemies, targets, hazards and incoming attacks remain readable during crowded magic/signature sequences |
| 1 | Encounter feel | Campaign enemies, resistant targets, corners and airborne pursuit behave well beyond the practice fixtures |
| 2 | Practice resources | Consider explicit reset of offensive spell stocks and Drive charge in the isolated arena, so repeated testing does not require returning to the launcher |
| 2 | Magic usability | Verify the native spell selection flow on a controller; make equipped spell availability and melee-earned refunds easy to understand |
| 2 | Crowd and wave tuning | Measure frame time, actor/effect capacity, collision and target selection before increasing density; preserve varied enemy pressure |
| 2 | HUD and accessibility | Inspect small windows, high DPI, remapped controls, focus changes and contrast against real battle backgrounds |
| 2 | Windows playable preview | Integrate the original packaging artwork and validate setup, campaign/practice save separation and launcher behavior |
| 3 | Updates | Add a dependable mod updater with atomic replacement and recovery after a validated Windows preview exists |

Use actual native captures for visual judgments and physical controller testing
for feel. Record the observed problem and comparison before tuning animation
speeds, cancel delays, enemy numbers or damage. The existing portable workflow
can continue protecting logic while those native checks are arranged.
