import { SUPPORTED_ROMS } from '../../rom-import.mjs';

const usedModules = new WeakSet();

function synchronize(fs, populate) {
    return new Promise((resolve, reject) => {
        fs.syncfs(populate, error => {
            if (error) {
                reject(new Error('The browser could not access local progress.', { cause: error }));
            } else {
                resolve();
            }
        });
    });
}

// Pass the canonical entry from inspectRom().supported after importing the
// cartridge into /data/extracted. Initialize a fresh module with noInitialRun.
export async function prepareBrowserProfile(module, rom, profile, { signal } = {}) {
    if (profile !== 'campaign' && profile !== 'practice') {
        throw new TypeError('Choose Campaign or Practice.');
    }
    if (!SUPPORTED_ROMS.includes(rom)) {
        throw new TypeError('Choose a supported Days cartridge.');
    }
    signal?.throwIfAborted();
    const fs = module?.FS;
    const idbfs = fs?.filesystems?.IDBFS;
    if (!idbfs || typeof fs.syncfs !== 'function' ||
        typeof module._khdays_pragmicat_select_mode !== 'function') {
        throw new Error('The Days browser module is missing local storage or the built-in mod.');
    }
    if (usedModules.has(module)) {
        throw new Error('Start a fresh game instance to change cartridges or modes.');
    }
    usedModules.add(module);

    const work = `/profiles/${profile}`;
    const practice = profile === 'practice';
    const combat = `${work}/mods/Pragmicat Mix`;
    for (const file of [`${combat}/mod.ini`, `${combat}/parameters.ini`, ...(practice ? [
        `${work}/mods/Pragmicat Practice/mod.ini`,
        `${work}/mods/Pragmicat Practice/scripts/main.lua`,
        `${work}/mods/Pragmicat Practice/missions/missions.txt`,
    ] : [])]) {
        if (!fs.analyzePath(file).exists) {
            throw new Error('The Days browser module is missing its packaged mods.');
        }
    }

    // Use the canonical cartridge hash so a dump with different unused header
    // padding still reaches the same saved progress.
    const storage = `/persistent/${rom.game_code}-${rom.sha256}/${profile}`;
    fs.mkdirTree(storage);
    fs.mount(idbfs, {}, storage);
    await synchronize(fs, true);
    signal?.throwIfAborted();

    const saves = `${storage}/saves`;
    const seed = (path, contents) => {
        if (!fs.analyzePath(path).exists) fs.writeFile(path, contents);
    };
    fs.mkdirTree(`${saves}/mods`);
    seed(`${saves}/settings.ini`,
        'preset = kh2\n\n[mods]\nnative_code = on\n' +
        (practice ? '\n[save]\nautosave = off\n' : ''));
    seed(`${saves}/mods/order.ini`,
        'Pragmicat Mix = on\n' + (practice ? 'Pragmicat Practice = on\n' : ''));
    if (practice) {
        const practiceOptions = `${saves}/mods/local.pragmicat-practice`;
        fs.mkdirTree(practiceOptions);
        seed(`${practiceOptions}/options.ini`, 'targets = 5\n');
    }
    await synchronize(fs, false);
    signal?.throwIfAborted();

    fs.mkdirTree('/data');
    fs.symlink('/data', `${work}/data`);
    fs.symlink(saves, `${work}/saves`);
    fs.chdir(work);
    if (module._khdays_pragmicat_select_mode(Number(practice)) !== 1) {
        throw new Error('The built-in Days mod refused the selected mode.');
    }

    // Serialize synchronization on this module so close and in-game save
    // requests cannot overlap on the same IndexedDB mount.
    let pending = Promise.resolve();
    const queue = operation => {
        const next = pending.then(operation);
        pending = next.catch(() => {});
        return next;
    };
    module.pragmicatCommitSave = (path, bytes) => queue(async () => {
        const components = path.split('/');
        if (components.shift() !== 'saves' || !components.length ||
            components.some(part => !part || part === '.' || part === '..')) {
            throw new Error('The save path is outside the active profile.');
        }
        const destination = `${saves}/${components.join('/')}`;
        const directory = destination.slice(0, destination.lastIndexOf('/'));
        fs.mkdirTree(directory);
        const temporary = destination + '.pragmicat.tmp';
        const previous = fs.analyzePath(destination).exists ? {
            bytes: fs.readFile(destination),
            stat: fs.stat(destination),
        } : null;
        fs.writeFile(temporary, bytes);
        let renamed = false;
        try {
            // IDBFS compares timestamps. A second save in the same tick must
            // still be newer than the version it is replacing.
            const time = Math.max(Date.now(), (previous?.stat.mtime.getTime() ?? 0) + 1);
            fs.utime(temporary, time, time);
            fs.rename(temporary, destination);
            renamed = true;
            await synchronize(fs, false);
        } catch (error) {
            if (renamed && previous) {
                fs.writeFile(destination, previous.bytes);
                fs.utime(destination, previous.stat.atime.getTime(), previous.stat.mtime.getTime());
            } else {
                fs.unlink(renamed ? destination : temporary);
            }
            throw error;
        }
    });
    return { flushSaves: () => queue(() => synchronize(fs, false)) };
}
