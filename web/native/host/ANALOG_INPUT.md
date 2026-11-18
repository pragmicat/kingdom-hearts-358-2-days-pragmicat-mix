# Browser movement and physical input ownership

The native movement hook accepts an analog heading only after the game's own
D-pad routine saw a held direction. The initial browser bridge supplied
`move_x/move_y` but no DS direction bits, so a stick alone left the native
heading at `-1` and could not start movement.

The bridge now synthesizes the matching eight-direction DS bits using the
upstream pure `khdays_stick_dpad` arithmetic, extracted into `stick_dpad.h` with
private names. A real-upstream comparison protects its behavior. The browser's
existing 20% movement dead zone is retained, with the native 5% hysteresis.
While active, movement uses the raw bounded stick angle and tilt, matching
native bindings and keeping its hysteresis tail consistent with the DS bits.
The camera retains its existing radial filtering.

Physical buttons and stick directions are stored separately and composed
before press-edge collection. Keyboard/gamepad button polling cannot erase the
stick's directions, and releasing/disconnecting the stick cannot erase a held
keyboard button. Both ordinary and field snapshots receive the directions.

Physical resets clear held input, analog state and pending taps while
preserving `ui_open`. Focus/visibility therefore cannot release ownership of an
open host menu. Only the explicit UI setter changes that ownership. Browser
window focus is tracked separately from `activeElement`, because a blurred
window can still report its canvas as the active element; its next RAF poll
must not restore a gamepad's held input.

## Verification

`run_test.py` now additionally compiles the real upstream `bindings.c`,
`controls_hooks.c`, `analog.c` and one original public decomp routine,
`Ov022_HeadingFromHeldDpad.c`. These source dependencies are pinned separately
in `UPSTREAM_ANALOG.json`. Initialize the pinned decomp submodule before the
host test; no ROM is needed:

```sh
git -C /path/to/khdays-port submodule update --init external/khdays-decomp
python3 web/native/host/run_test.py --upstream /path/to/khdays-port --emcc /path/to/emsdk/upstream/emscripten/emcc
node web/native/host/tests/browser-input.test.mjs
```

The real Wasm checks cover all eight directions, continuous comparison with
native sector hysteresis, mixed source ordering, rest/disconnect, dead-zone
entry/exit, camera-only input, and an actual original D-pad heading passed
through the native analog hook. Physical reset and visibility ownership are
also covered. The JavaScript regression checks that a window-blur reset stays
cleared on the next gamepad poll even if the canvas remains activeElement.

The stick-direction and menu-reset Wasm regressions and the window-blur JS
regression all failed against the previous bridge. The corrected complete
host suite passes at `-O1` and `-O2`. These are ROM-free native-component checks,
not observed game movement or controller-feel validation. No original game C,
ROM, extracted data or compiled output is copied into this repository.
