import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { SUPPORTED_ROMS } from '../../../rom-import.mjs';
import { prepareBrowserProfile } from '../profile.mjs';

const createModule = (await import(pathToFileURL(resolve(process.argv[2])))).default;

for (const mode of ['campaign', 'practice']) {
    const module = await createModule();
    assert.equal(module._pragmicat_check_builtin(), 1);
    const fs = module.FS;
    const rom = SUPPORTED_ROMS[0];
    const saves = `/persistent/${rom.game_code}-${rom.sha256}/${mode}/saves`;
    // The real Emscripten filesystem and module run in Node. Stand in for
    // IndexedDB hydration to verify sequencing and preservation of prior files.
    fs.filesystems.IDBFS = fs.filesystems.MEMFS;
    const syncCalls = [];
    let failSave = false;
    fs.syncfs = (populate, done) => {
        syncCalls.push(populate);
        if (populate) {
            assert.equal(fs.analyzePath(saves).exists, false);
            fs.mkdirTree(saves);
            fs.writeFile(`${saves}/settings.ini`, 'existing settings');
        }
        done(failSave ? new Error('Simulated quota failure') : null);
    };
    const session = await prepareBrowserProfile(module, rom, mode);
    assert.equal(module._pragmicat_is_practice(), Number(mode === 'practice'));
    assert.equal(fs.cwd(), `/profiles/${mode}`);
    assert.equal(fs.readFile('saves/settings.ini', { encoding: 'utf8' }), 'existing settings');
    assert.equal(fs.readlink(`${fs.cwd()}/data`), '/data');
    assert.equal(fs.analyzePath('mods/Pragmicat Practice/scripts/main.lua').exists, mode === 'practice');
    if (mode === 'practice') {
        assert.equal(fs.readFile('saves/mods/local.pragmicat-practice/options.ini', {
            encoding: 'utf8',
        }), 'targets = 5\n');
    }
    await Promise.all([session.flushSaves(), session.flushSaves()]);
    assert.deepEqual(syncCalls, [true, false, false, false]);
    await assert.rejects(prepareBrowserProfile(module, rom, mode), /fresh game instance/);
    const save = text => module.ccall('pragmicat_save_probe', 'number', ['string'], [text], { async: true });
    const path = 'saves/slots/probe.sav';
    assert.equal(await save('existing progress'), 1);
    failSave = true;
    assert.equal(await save('quota error must preserve that progress'), 0);
    assert.equal(fs.readFile(path, { encoding: 'utf8' }), 'existing progress');
    assert.equal(fs.analyzePath(path + '.pragmicat.tmp').exists, false);
    failSave = false;
    assert.equal(await save('next progress'), 1);
    assert.equal(fs.readFile(path, { encoding: 'utf8' }), 'next progress');
}
console.log('Built-in Wasm combat, profile isolation and rollback on failed saves passed.');
