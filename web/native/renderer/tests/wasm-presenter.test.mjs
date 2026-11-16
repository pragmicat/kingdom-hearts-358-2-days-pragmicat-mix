import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { pathToFileURL } from "node:url";
import { attachDaysCanvas } from "../canvas-presenter.mjs";

const path = process.argv[2];
if (!path) throw new Error("Pass the built days_software_renderer.mjs path.");
const url = pathToFileURL(path);
const { default: createRenderer } = await import(url);
const module = await createRenderer({ wasmBinary: await readFile(new URL("./days_software_renderer.wasm", url)) });
let displayed;
const context = {
  createImageData(w, h) { return { data: new Uint8ClampedArray(w * h * 4) }; },
  putImageData(image) { displayed = image; },
};
const canvas = { width: 0, height: 0, getContext() { return context; } };
const presenter = attachDaysCanvas(module, canvas);
const bytes = 256 * 192 * 4;
const pixels = module._malloc(bytes * 2);
assert.notEqual(pixels, 0);
// Two actual native-format framebuffers, as produced by display.c.
const words = new Uint32Array(module.HEAPU8.buffer, pixels, 256 * 192 * 2);
words[0] = 0x00123456;
words[256 * 192] = 0x00ff0080;
assert.equal(module._days_web_present(pixels, pixels + bytes), 1);
assert.deepEqual([...displayed.data.slice(0, 4)], [18, 52, 86, 255]);
assert.deepEqual([...displayed.data.slice(bytes, bytes + 4)], [255, 0, 128, 255]);
assert.equal(presenter.frameCount, 1);
assert.equal(module._days_web_frame_generation(), 1);
assert.equal(canvas.width, 256);
assert.equal(canvas.height, 384);
assert.equal(module._days_web_render(0, 0, 0), 0);
assert.equal(presenter.frameCount, 1);

// Force real WebAssembly.Memory growth, then present through the same callback.
const oldBuffer = module.HEAPU8.buffer;
const expanded = module._malloc(oldBuffer.byteLength + 1024);
assert.notEqual(expanded, 0);
assert.notEqual(module.HEAPU8.buffer, oldBuffer);
new Uint32Array(module.HEAPU8.buffer, pixels, 1)[0] = 0x00010203;
assert.equal(module._days_web_present(pixels, pixels + bytes), 1);
assert.deepEqual([...displayed.data.slice(0, 4)], [1, 2, 3, 255]);
assert.equal(presenter.frameCount, 2);
module._free(expanded);
module._free(pixels);
presenter.dispose();
console.log("actual Wasm framebuffer→Canvas bridge + real memory growth: passed");
