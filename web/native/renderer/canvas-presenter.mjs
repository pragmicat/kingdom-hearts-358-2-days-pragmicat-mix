/* Connect the Wasm CPU renderer to a real Canvas2D surface. No ROM or assets are
 * fetched here. This module is a display adapter, not a game emulator/runtime. */
export function attachDaysCanvas(module, canvas) {
  const context = canvas?.getContext?.("2d", { alpha: false });
  if (!context) throw new Error("A Canvas2D surface is required.");
  const previous = module.onDaysFrame;
  let active = true;
  let image = null;
  let frameCount = 0;

  function present(pointer, width, height, generation) {
    if (!active) return;
    if (!Number.isInteger(pointer) || pointer < 0 || width !== 256 || height !== 384) {
      throw new Error("Invalid Days software framebuffer.");
    }
    // Read the current heap for every frame: ALLOW_MEMORY_GROWTH replaces its
    // ArrayBuffer, and retaining a view would show stale or detached pixels.
    const heap = module.HEAPU8;
    const bytes = width * height * 4;
    if (!(heap instanceof Uint8Array) || pointer > heap.byteLength - bytes) {
      throw new Error("Days framebuffer is outside WebAssembly memory.");
    }
    if (canvas.width !== width) canvas.width = width;
    if (canvas.height !== height) canvas.height = height;
    if (!image) image = context.createImageData(width, height);
    image.data.set(heap.subarray(pointer, pointer + bytes));
    context.putImageData(image, 0, 0);
    frameCount++;
    if (typeof previous === "function") previous(pointer, width, height, generation);
  }

  module.onDaysFrame = present;
  return {
    get frameCount() { return frameCount; },
    dispose() {
      active = false;
      if (module.onDaysFrame === present) module.onDaysFrame = previous;
    },
  };
}
