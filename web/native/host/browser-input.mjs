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

export function attachDaysInput(module, canvas, {
  windowObject = window, documentObject = document, navigatorObject = navigator,
} = {}) {
  for (const suffix of API) if (typeof module?.[`_days_web_${suffix}`] !== 'function')
    throw new Error(`The native Days host is missing days_web_${suffix}.`);
  if (!canvas?.addEventListener || !canvas.getBoundingClientRect)
    throw new TypeError('A Days display canvas is required.');
  const keys = new Set();
  let active = true, raf, padBits = 0, sentButtons = -1, lastSticks = '', pointer = null;
  let windowFocused = documentObject.hasFocus?.() ?? true;
  const oldTabIndex = canvas.tabIndex;
  const oldTouchAction = canvas.style.touchAction;
  if (canvas.tabIndex < 0) canvas.tabIndex = 0;
  canvas.style.touchAction = 'none';
  const focused = () => windowFocused && !documentObject.hidden && (documentObject.activeElement === canvas || canvas.contains?.(documentObject.activeElement));
  function publishButtons() {
    let bits = padBits;
    for (const key of keys) bits |= DAYS_KEYS[key];
    if (bits !== sentButtons) { module._days_web_input_buttons(bits); sentButtons = bits; }
  }
  function sticks(lx, ly, rx, ry, connected) {
    const state = [lx, ly, rx, ry, connected];
    const identity = state.join(',');
    if (identity !== lastSticks) { module._days_web_input_sticks(...state); lastSticks = identity; }
  }
  function reset() {
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
  function touch(event, down) {
    const rect = canvas.getBoundingClientRect();
    if (rect.width <= 0 || rect.height <= 0) return;
    const x = (event.clientX - rect.left) * 256 / rect.width;
    const y = (event.clientY - rect.top) * 384 / rect.height - 192;
    const inside = x >= 0 && x < 256 && y >= 0 && y < 192;
    module._days_web_input_touch(Math.max(0, Math.min(255, Math.floor(x))),
      Math.max(0, Math.min(191, Math.floor(y))), down && inside ? 1 : 0);
  }
  function pointerdown(event) {
    if (!active || documentObject.hidden || event.button !== 0 || pointer !== null) return;
    canvas.focus();
    const rect = canvas.getBoundingClientRect();
    if (event.clientY < rect.top + rect.height / 2) return; // upper screen has no stylus
    event.preventDefault(); pointer = event.pointerId;
    canvas.setPointerCapture?.(pointer); touch(event, true);
  }
  function pointermove(event) { if (event.pointerId === pointer) { event.preventDefault(); touch(event, true); } }
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
      const pads = navigatorObject.getGamepads?.() || [];
      pad = Array.from(pads).find(candidate => candidate?.connected && candidate.mapping === 'standard');
    }
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
  const listeners = [
    [windowObject, 'keydown', keydown], [windowObject, 'keyup', keyup],
    [windowObject, 'blur', windowBlur], [windowObject, 'focus', windowFocus], [canvas, 'blur', reset],
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
      if (notifyRuntime) {
        reset();
        module._days_web_host_hidden(0);
      }
      canvas.tabIndex = oldTabIndex; canvas.style.touchAction = oldTouchAction;
    },
  };
}
