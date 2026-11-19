/* Connect the Wasm CPU renderer to a real Canvas2D surface. No ROM or assets are
 * fetched here. This module is a display adapter, not a game emulator/runtime. */
export function attachDaysCanvas(module, canvas) {
  const context = canvas?.getContext?.("2d", { alpha: false });
  if (!context) throw new Error("A Canvas2D surface is required.");
  const previous = module.onDaysFrame;
  const previousHud = module.onDaysDriveHud;
  let active = true;
  // Both native screens keep all their pixels. Put the optional Drive panel
  // in the hinge between them, at the same pixel scale as the game.
  const screenBytes = 256 * 192 * 4;
  let hinge = null;
  let image = null;
  let frameCount = 0;

  function driveHud(pointer, width, height) {
    if (!active) return;
    if (!Number.isInteger(width) || !Number.isInteger(height) ||
        width < 0 || width > 256 || height < 0 || height > 32) return;
    const heap = module.HEAPU8;
    const size = width * height * 4;
    if (!Number.isInteger(pointer) || pointer < 0 || (pointer && pointer > heap.byteLength - size)) return;
    if (!height) {
      hinge = null;
    } else {
      if (hinge?.height !== height) hinge = context.createImageData(256, height);
      const left = Math.floor((256 - width) / 2);
      // The native HUD uses premultiplied BGRA. Composite it on the opaque
      // hinge before the callback returns and its temporary raster is freed.
      for (let y = 0; y < height; ++y) {
        for (let x = 0; x < 256; ++x) {
          const dest = (y * 256 + x) * 4;
          const inside = pointer && x >= left && x < left + width;
          const src = pointer + (y * width + x - left) * 4;
          const inverse = inside ? 255 - heap[src + 3] : 255;
          hinge.data[dest] = (inside ? heap[src + 2] : 0) + 10 * inverse / 255;
          hinge.data[dest + 1] = (inside ? heap[src + 1] : 0) + 17 * inverse / 255;
          hinge.data[dest + 2] = (inside ? heap[src] : 0) + 24 * inverse / 255;
          hinge.data[dest + 3] = 255;
        }
      }
      if (image?.height === 384 + height) image.data.set(hinge.data, screenBytes);
    }
    if (typeof previousHud === "function") previousHud(pointer, width, height);
  }

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
    const displayHeight = height + (hinge?.height ?? 0);
    if (canvas.width !== width) canvas.width = width;
    if (canvas.height !== displayHeight) canvas.height = displayHeight;
    if (!image || image.height !== displayHeight) {
      image = context.createImageData(width, displayHeight);
      if (hinge) image.data.set(hinge.data, screenBytes);
    }
    image.data.set(heap.subarray(pointer, pointer + screenBytes));
    image.data.set(heap.subarray(pointer + screenBytes, pointer + bytes),
                  image.data.length - screenBytes);
    context.putImageData(image, 0, 0);
    frameCount++;
    if (typeof previous === "function") previous(pointer, width, height, generation);
  }

  module.onDaysDriveHud = driveHud;
  module.onDaysFrame = present;
  return {
    get frameCount() { return frameCount; },
    dispose() {
      active = false;
      if (module.onDaysDriveHud === driveHud) module.onDaysDriveHud = previousHud;
      if (module.onDaysFrame === present) module.onDaysFrame = previous;
    },
  };
}
