import { importRomToFileSystem, inspectRom } from '../rom-import.mjs';
import { attachDaysAudio } from './host/browser-audio.mjs';
import { attachDaysInput } from './host/browser-input.mjs';
import { prepareBrowserProfile } from './mod/profile.mjs';
import { attachDaysCanvas } from './renderer/canvas-presenter.mjs';
import { attachDaysNotices } from './start/browser-notices.mjs';

// Prepare a cartridge before presenting the Play button. Launch only with a
// factory from the built Days recompilation and a fresh user gesture for sound.
export async function prepareDaysGame(createRuntime, file, canvas, {
    profile = 'campaign',
    signal,
    onProgress,
    onFirstFrame = () => {},
    onError = () => {},
    onExit = () => {},
} = {}) {
    let phase = 'preparing';
    let cleanup = () => {};
    const fail = error => {
        if (phase === 'ended') return;
        phase = 'ended';
        cleanup();
        onError(error);
    };
    const inspection = await inspectRom(file, { signal, onProgress });
    signal?.throwIfAborted();
    const module = await createRuntime({
        noInitialRun: true,
        onAbort: reason => fail(new Error(String(reason))),
        onExit: status => {
            if (status || phase === 'starting') {
                fail(new Error(`Days exited before it could continue (status ${status}).`));
            } else if (phase === 'playing') {
                phase = 'ended';
                cleanup();
                onExit();
            }
        },
    });
    if (!module.FS || typeof module.callMain !== 'function' || !module.HEAPU8 ||
        typeof module._days_web_drain_mod_notices !== 'function') {
        throw new Error('The complete Days browser runtime is required.');
    }
    await importRomToFileSystem(inspection, module.FS, { signal, onProgress });
    const saves = await prepareBrowserProfile(module, inspection.supported, profile, { signal });
    signal?.throwIfAborted();
    phase = 'ready';

    return {
        flushSaves: saves.flushSaves,
        async start() {
            if (phase !== 'ready') throw new Error('Start a fresh Days session to play again.');
            signal?.throwIfAborted();
            phase = 'starting';
            const adapters = [];
            cleanup = () => {
                for (const adapter of adapters.splice(0).reverse()) {
                    Promise.resolve(adapter.dispose({ notifyRuntime: false })).catch(onError);
                }
            };
            try {
                module.onDaysFrame = () => {
                    if (phase === 'ended') return;
                    module._days_web_drain_mod_notices();
                    if (phase !== 'starting') return;
                    phase = 'playing';
                    onFirstFrame();
                };
                module.onDaysRuntimeProblem = (where, problem) => {
                    fail(new Error(problem || `Days could not load game files in ${where}.`));
                };
                adapters.push(attachDaysNotices(module, canvas));
                adapters.push(attachDaysCanvas(module, canvas));
                const input = attachDaysInput(module, canvas);
                adapters.push(input);
                const audio = attachDaysAudio(module, { onError });
                adapters.push(audio);
                // Ask for audio access in the Play gesture. If the browser
                // declines it, the game can still start muted.
                input.focus();
                try {
                    await audio.resume();
                } catch (error) {
                    onError(error);
                }
                signal?.throwIfAborted();
                module.callMain([]);
            } catch (error) {
                fail(error);
                throw error;
            }
        },
    };
}
