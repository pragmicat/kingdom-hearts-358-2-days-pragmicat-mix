import assert from "node:assert/strict";
import { attachDaysCanvas } from "../canvas-presenter.mjs";

const bytes = 256 * 384 * 4;
let displayed;
let previousCalls = 0;
const module = { HEAPU8: new Uint8Array(bytes + 8), onDaysFrame() { previousCalls++; } };
const context = {
  createImageData(w, h) { return { data: new Uint8ClampedArray(w * h * 4) }; },
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
const owned = module.onDaysFrame;
presenter.dispose();
owned(0, 256, 384, 3);
assert.equal(presenter.frameCount, 2);
assert.throws(() => attachDaysCanvas(module, {}), /Canvas2D/);
console.log("Canvas bridge + memory growth + disposal: passed");
