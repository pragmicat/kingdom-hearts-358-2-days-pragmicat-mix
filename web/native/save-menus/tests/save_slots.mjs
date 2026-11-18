import assert from 'node:assert/strict';
import { pathToFileURL } from 'node:url';
import { resolve } from 'node:path';

const modRoot = resolve(process.argv[3]);
const createModule = (await import(pathToFileURL(resolve(process.argv[2])))).default;
const { prepareBrowserProfile } = await import(pathToFileURL(resolve(modRoot, 'web/native/mod/profile.mjs')));
const { SUPPORTED_ROMS } = await import(pathToFileURL(resolve(modRoot, 'web/rom-import.mjs')));

const module = await createModule({ noInitialRun: true });
const fs = module.FS;
// The native slots test exercises the production browser commit and rollback.
// The IDBFS completion callback is simulated in Node, including one failure.
fs.filesystems.IDBFS = fs.filesystems.MEMFS;
let rejected = 0;
fs.syncfs = (populate, callback) => {
    if (!populate && module.daysTestRefuseSync) {
        rejected++;
        callback(new Error('simulated IndexedDB commit refusal'));
    } else {
        callback(null);
    }
};

const mods = '/profiles/campaign/mods/Pragmicat Mix';
fs.mkdirTree(mods);
fs.writeFile(`${mods}/mod.ini`, '');
fs.writeFile(`${mods}/parameters.ini`, '');
await prepareBrowserProfile(module, SUPPORTED_ROMS[0], 'campaign');
assert.equal(await module.ccall('days_test_save_slots', 'number', [], [], { async: true }), 0);
assert.equal(rejected, 1);
