import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

const createModule = (await import(pathToFileURL(resolve(process.argv[2])))).default;
const problems = [];
const frames = [];
let closed = 0;
const module = await createModule({
    onDaysRuntimeProblem(where, problem) {
        problems.push({ where, problem });
    },
    onDaysRuntimeClosed() {
        closed++;
    },
    printErr() {},
});
assert.equal(module._days_web_test_host_start(), 0);
assert.equal(problems[0].where, 'Canvas');
module.onDaysFrame = (pixels, width, height, generation) => {
    const heap = module.HEAPU8;
    frames.push({
        width, height, generation,
        upper: Array.from(heap.slice(pixels, pixels + 4)),
        lower: Array.from(heap.slice(pixels + 256 * 192 * 4, pixels + 256 * 192 * 4 + 4)),
    });
};
assert.equal(module._days_web_test_host_start(), 1);
assert.equal(module._days_web_test_host_frame(), 1);
assert.deepEqual(frames, [{
    width: 256, height: 384, generation: 1,
    upper: [255, 172, 16, 255], lower: [12, 19, 255, 255],
}]);
module._days_web_test_no_data();
assert.deepEqual(problems.slice(1), [
    { where: 'data/extracted', problem: 'Choose a supported Days ROM before starting.' },
    { where: 'data/extracted/sample', problem: 'corrupt manifest' },
]);
module._days_web_test_host_close();
module._days_web_test_host_close();
assert.equal(closed, 1);
console.log('Wasm native host display contract passed.');
