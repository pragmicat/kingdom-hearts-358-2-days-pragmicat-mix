/* DOM/gamepad source for the real khdays-port host input bridge. This module
 * starts no game and fetches no ROM or runtime. Attach after Wasm initialization. */
export const DAYS_KEYS = Object.freeze({
  KeyJ: 1 << 0, KeyK: 1 << 1, ShiftLeft: 1 << 2, ShiftRight: 1 << 2,
  Enter: 1 << 3, ArrowRight: 1 << 4, ArrowLeft: 1 << 5,
  ArrowUp: 1 << 6, ArrowDown: 1 << 7, KeyE: 1 << 8, KeyQ: 1 << 9,
  KeyU: 1 << 10, KeyI: 1 << 11,
});
const PAD_BITS = Object.freeze({0:1, 1:2, 2:2048, 3:1024, 4:512, 5:256,
  8:4, 9:8, 12:64, 13:128, 14:32, 15:16});
const API = ['input_reset', 'input_buttons', 'input_touch', 'input_sticks', 'host_hidden'];
const editable = target => Boolean(target?.isContentEditable || /^(INPUT|TEXTAREA|SELECT)$/.test(target?.tagName || ''));
const rawAxis = value => Number.isFinite(value) ? Math.round(Math.max(-1, Math.min(1, value)) * 32767) : 0;
let lastPadToken = 0;

export function attachDaysInput(module, canvas, {
  windowObject = window, documentObject = document, navigatorObject = navigator,
} = {}) {
  for (const suffix of API) if (typeof module?.[`_days_web_${suffix}`] !== 'function')
    throw new Error(`The native Days host is missing days_web_${suffix}.`);
  if (!canvas?.addEventListener || !canvas.getBoundingClientRect)
    throw new TypeError('A Days display canvas is required.');
  const keys = new Set();
  let active = true, raf, padBits = 0, sentButtons = -1, lastSticks = '', pointer = null;
  let selectedPad, padToken = 0, lightPad = 0, techniquePad = 0, playingImpact;
  let windowFocused = documentObject.hasFocus?.() ?? true;
  const oldTabIndex = canvas.tabIndex;
  const oldTouchAction = canvas.style.touchAction;
  if (canvas.tabIndex < 0) canvas.tabIndex = 0;
  canvas.style.touchAction = 'none';
  const focused = () => windowFocused && !documentObject.hidden && (documentObject.activeElement === canvas || canvas.contains?.(documentObject.activeElement));
  function publishButtons() {
    let bits = padBits;
    for (const key of keys) bits |= DAYS_KEYS[key];
    if (bits === sentButtons) return;
    // Record who made the combined button rise. Another source holding the
    // same button later cannot take ownership of an already-buffered attack.
    const rising = bits & ~(sentButtons < 0 ? 0 : sentButtons);
    if (rising & DAYS_KEYS.KeyJ) lightPad = padBits & DAYS_KEYS.KeyJ ? padToken : 0;
    if (rising & DAYS_KEYS.KeyU) techniquePad = padBits & DAYS_KEYS.KeyU ? padToken : 0;
    sentButtons = bits;
    module._days_web_input_buttons(bits);
  }
  function stopImpact() {
    const previous = playingImpact;
    playingImpact = undefined;
    if (previous) {
      try { Promise.resolve(previous.actuator.reset()).catch(() => {}); }
      catch { /* Disconnected actuator. */ }
    }
  }
  function selectPad(pad) {
    if (Boolean(pad) !== Boolean(selectedPad) ||
        (pad && (pad.index !== selectedPad.index || pad.id !== selectedPad.id))) {
      stopImpact();
      lightPad = techniquePad = 0;
      if (pad) lastPadToken = ((lastPadToken + 1) >>> 0) || 1;
      padToken = pad ? lastPadToken : 0;
    }
    selectedPad = pad;
  }
  function attackPad(pressed) {
    if (!active || !focused() || !selectedPad?.connected) return 0;
    if (pressed & DAYS_KEYS.KeyU) return techniquePad;
    return pressed & DAYS_KEYS.KeyJ ? lightPad : 0;
  }
  function combatImpact(token, ending) {
    if (token === 0) { stopImpact(); return; }
    if (!active || (token >>> 0) !== padToken || !focused() || !selectedPad?.connected) return;
    try {
      const actuator = selectedPad.vibrationActuator;
      if (!actuator?.playEffect || !actuator.reset) return;
      const effect = { actuator };
      const completion = actuator.playEffect('dual-rumble', ending
        ? { duration: 52, weakMagnitude: 0.25, strongMagnitude: 0.18 }
        : { duration: 32, weakMagnitude: 0.16, strongMagnitude: 0.06 });
      playingImpact = effect;
      Promise.resolve(completion).catch(() => {}).then(() => {
        if (playingImpact === effect) playingImpact = undefined;
      });
    } catch { /* The controller or browser declined optional feedback. */ }
  }
  function sticks(lx, ly, rx, ry, connected) {
    const state = [lx, ly, rx, ry, connected];
    const identity = state.join(',');
    if (identity !== lastSticks) { module._days_web_input_sticks(...state); lastSticks = identity; }
  }
  function reset() {
    selectPad(undefined);
    keys.clear(); padBits = 0; sentButtons = -1; lastSticks = ''; pointer = null;
    module._days_web_input_reset();
  }
  function windowBlur() { windowFocused = false; reset(); }
  function windowFocus() { windowFocused = true; }
  function keydown(event) {
    if (!active || !focused() || editable(event.target) || event.ctrlKey || event.altKey || event.metaKey || !(event.code in DAYS_KEYS)) return;
    event.preventDefault();
    if (!keys.has(event.code)) { keys.add(event.code); publishButtons(); }
  }
  function keyup(event) {
    if (!keys.has(event.code)) return;
    event.preventDefault(); keys.delete(event.code); publishButtons();
  }
  // The lower touchscreen always occupies the final 192 framebuffer rows,
  // including when the mod displays its Drive gauge between the screens.
  function stylusPoint(event) {
    const rect = canvas.getBoundingClientRect();
    if (rect.width <= 0 || rect.height <= 0 || canvas.height < 384) return null;
    const x = Math.floor((event.clientX - rect.left) * 256 / rect.width);
    const y = Math.floor((event.clientY - rect.top) * canvas.height / rect.height) - (canvas.height - 192);
    return x >= 0 && x < 256 && y >= 0 && y < 192 ? { x, y } : null;
  }
  function pointerdown(event) {
    if (!active || documentObject.hidden || event.button !== 0 || pointer !== null) return;
    canvas.focus();
    const point = stylusPoint(event);
    if (!point) return;
    event.preventDefault();
    pointer = event.pointerId;
    canvas.setPointerCapture?.(pointer);
    module._days_web_input_touch(point.x, point.y, 1);
  }
  function pointermove(event) {
    if (event.pointerId !== pointer) return;
    event.preventDefault();
    const point = stylusPoint(event);
    module._days_web_input_touch(point?.x ?? 0, point?.y ?? 0, point ? 1 : 0);
  }
  function pointerup(event) {
    if (event.pointerId !== pointer) return;
    module._days_web_input_touch(0, 0, 0); pointer = null;
  }
  function visibility() {
    reset();
    module._days_web_host_hidden(documentObject.hidden ? 1 : 0);
  }
  function poll() {
    if (!active) return;
    let pad;
    if (focused()) {
      try {
        const pads = navigatorObject.getGamepads?.() || [];
        pad = Array.from(pads).find(candidate => candidate?.connected && candidate.mapping === 'standard');
      } catch { /* The browser can disable gamepad access for this page. */ }
    }
    selectPad(pad);
    padBits = 0;
    if (pad) {
      for (const [index, bit] of Object.entries(PAD_BITS)) {
        const button = pad.buttons?.[Number(index)];
        if (button?.pressed || button?.value >= 0.5) padBits |= bit;
      }
      sticks(...[0,1,2,3].map(index => rawAxis(pad.axes?.[index])), 1);
    } else sticks(0, 0, 0, 0, 0);
    publishButtons();
    raf = windowObject.requestAnimationFrame(poll);
  }
  function disconnect(event) {
    if (!selectedPad || event.gamepad?.index !== selectedPad.index) return;
    selectPad(undefined);
    padBits = 0;
    publishButtons();
    sticks(0, 0, 0, 0, 0);
  }
  module.onDaysAttackPad = attackPad;
  module.onDaysCombatImpact = combatImpact;
  const listeners = [
    [windowObject, 'keydown', keydown], [windowObject, 'keyup', keyup],
    [windowObject, 'blur', windowBlur], [windowObject, 'focus', windowFocus], [canvas, 'blur', reset],
    [windowObject, 'gamepaddisconnected', disconnect],
    [canvas, 'pointerdown', pointerdown], [canvas, 'pointermove', pointermove],
    [windowObject, 'pointerup', pointerup], [windowObject, 'pointercancel', pointerup],
    [canvas, 'lostpointercapture', pointerup], [documentObject, 'visibilitychange', visibility],
  ];
  for (const [target, event, callback] of listeners) target.addEventListener(event, callback);
  reset(); module._days_web_host_hidden(documentObject.hidden ? 1 : 0);
  raf = windowObject.requestAnimationFrame(poll);
  return {
    reset,
    focus() { canvas.focus(); },
    dispose({ notifyRuntime = true } = {}) {
      if (!active) return;
      active = false; windowObject.cancelAnimationFrame(raf);
      for (const [target, event, callback] of listeners) target.removeEventListener(event, callback);
      selectPad(undefined);
      if (module.onDaysAttackPad === attackPad) delete module.onDaysAttackPad;
      if (module.onDaysCombatImpact === combatImpact) delete module.onDaysCombatImpact;
      if (notifyRuntime) {
        reset();
        module._days_web_host_hidden(0);
      }
      canvas.tabIndex = oldTabIndex; canvas.style.touchAction = oldTouchAction;
    },
  };
}
