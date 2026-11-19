import assert from "node:assert/strict";
import { attachDaysCanvas } from "../canvas-presenter.mjs";

const bytes = 256 * 384 * 4;
let displayed;
let previousCalls = 0;
const module = { HEAPU8: new Uint8Array(bytes + 8), onDaysFrame() { previousCalls++; } };
const context = {
  createImageData(w, h) { return { width: w, height: h, data: new Uint8ClampedArray(w * h * 4) }; },
  putImageData(image, x, y) { assert.equal(x, 0); assert.equal(y, 0); displayed = image; },
};
const canvas = { width: 0, height: 0, getContext() { return context; } };
const presenter = attachDaysCanvas(module, canvas);
module.HEAPU8.set([18, 52, 86, 255], 8);
module.onDaysFrame(8, 256, 384, 1);
assert.deepEqual([...displayed.data.slice(0, 4)], [18, 52, 86, 255]);
assert.equal(canvas.width, 256);
assert.equal(canvas.height, 384);
assert.equal(presenter.frameCount, 1);
// Replace the memory buffer exactly as Wasm memory growth does.
module.HEAPU8 = new Uint8Array(bytes * 2);
module.HEAPU8.set([255, 0, 128, 255], 16);
module.onDaysFrame(16, 256, 384, 2);
assert.deepEqual([...displayed.data.slice(0, 4)], [255, 0, 128, 255]);
assert.equal(previousCalls, 2);
assert.throws(() => module.onDaysFrame(bytes * 2, 256, 384, 3), /outside/);
assert.throws(() => module.onDaysFrame(0, 512, 384, 3), /Invalid/);
assert.equal(presenter.frameCount, 2);
// A tiny native premultiplied BGRA panel, between the two full game screens.
const panel = bytes + 24;
module.HEAPU8.set([0, 0, 128, 128, 255, 152, 55, 255], panel);
module.onDaysDriveHud(panel, 2, 1);
module.HEAPU8.fill(0, panel, panel + 8); // the temporary native raster is gone
module.HEAPU8.set([7, 31, 52, 255], 16 + bytes / 2);
module.onDaysFrame(16, 256, 384, 3);
const pixel = (x, y) => [...displayed.data.slice(4 * (y * 256 + x), 4 * (y * 256 + x + 1))];
assert.equal(canvas.height, 385);
assert.deepEqual(pixel(127, 192), [133, 8, 12, 255]);
assert.deepEqual(pixel(128, 192), [55, 152, 255, 255]);
assert.deepEqual(pixel(0, 193), [7, 31, 52, 255]);
module.onDaysDriveHud(0, 2, 1); // pause keeps the screens in place
module.onDaysFrame(16, 256, 384, 4);
assert.deepEqual(pixel(127, 192), [10, 17, 24, 255]);
module.onDaysDriveHud(0, 0, 0); // turning the meter off restores the original layout
module.onDaysFrame(16, 256, 384, 5);
assert.equal(canvas.height, 384);
assert.deepEqual(pixel(0, 192), [7, 31, 52, 255]);
const owned = module.onDaysFrame;
presenter.dispose();
owned(0, 256, 384, 6);
assert.equal(presenter.frameCount, 5);
assert.equal(module.onDaysDriveHud, undefined);
assert.throws(() => attachDaysCanvas(module, {}), /Canvas2D/);
console.log("Canvas bridge + memory growth + disposal: passed");
