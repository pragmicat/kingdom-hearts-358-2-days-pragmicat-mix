/* WebAudio sink for the actual native DS mixer. No context is constructed or
 * resumed until resume() is called by the page's Play/Enable sound gesture. */
export const DAYS_AUDIO_RATE = 32728;
const LEAD_SECONDS = 0.030;
const QUEUE_SECONDS = 0.100;
const MAX_FRAMES = 512;

export function attachDaysAudio(module, {
  AudioContextClass = globalThis.AudioContext || globalThis.webkitAudioContext,
  documentObject = globalThis.document,
  onStatus = () => {}, onError = () => {},
} = {}) {
  if (!module || typeof module._days_web_audio_set_device_rate !== 'function')
    throw new Error('The native Days audio host is unavailable.');
  const previous = {audio:module.onDaysAudio, control:module.onDaysAudioControl, problem:module.onDaysAudioProblem};
  let context = null, gain = null, disposed = false, nextTime = 0;
  let open = true, hostPaused = false, userPaused = false, muted = false, hidden = Boolean(documentObject?.hidden);
  let volume = 1, acceptedBlocks = 0, droppedBlocks = 0;
  const sources = new Set();
  function clearQueue() {
    for (const source of sources) {
      source.onended = null;
      try { source.stop(); } catch {}
      source.disconnect();
    }
    sources.clear(); nextTime = 0;
  }
  function unavailable() { return !open || hostPaused || userPaused || muted || hidden || !context || context.state !== 'running'; }
  function stateChanged() {
    if (context?.state !== 'running') clearQueue();
    onStatus(context?.state === 'running' ? 'running' : context?.state || 'off');
  }
  function receive(pointer, frames, rate, format) {
    if (disposed) return false;
    const width = format === 0 ? 2 : format === 1 ? 4 : 0;
    if (!Number.isSafeInteger(pointer) || pointer < 0 || !width || pointer % width ||
        !Number.isInteger(frames) || frames < 1 || frames > MAX_FRAMES ||
        !Number.isInteger(rate) || rate < 8000 || rate > 192000)
      throw new RangeError('Invalid native Days audio block.');
    // Linear memory can grow between packets. Always read the current heap;
    // AudioBuffer receives its own copy before the native mixer reuses its array.
    const heap = module.HEAPU8;
    const bytes = frames * 2 * width;
    if (!(heap instanceof Uint8Array) || pointer > heap.byteLength - bytes)
      throw new RangeError('Native Days audio block is outside Wasm memory.');
    if (unavailable()) { droppedBlocks++; return false; }
    const now = context.currentTime;
    if (nextTime - now > QUEUE_SECONDS) { droppedBlocks++; return false; }
    const buffer = context.createBuffer(2, frames, rate);
    const data = new DataView(heap.buffer, heap.byteOffset + pointer, bytes);
    for (let side = 0; side < 2; side++) {
      const channel = buffer.getChannelData(side);
      for (let frame = 0; frame < frames; frame++) {
        const offset = (frame * 2 + side) * width;
        const sample = format === 0 ? data.getInt16(offset, true) / 32768 : data.getFloat32(offset, true);
        channel[frame] = Number.isFinite(sample) ? Math.max(-1, Math.min(1, sample)) : 0;
      }
    }
    const source = context.createBufferSource();
    source.buffer = buffer; source.connect(gain); sources.add(source);
    source.onended = () => { sources.delete(source); source.disconnect(); };
    const when = nextTime > now ? nextTime : now + LEAD_SECONDS;
    try { source.start(when); }
    catch (error) { sources.delete(source); source.disconnect(); onError(error); droppedBlocks++; return false; }
    nextTime = when + frames / rate;
    acceptedBlocks++;
    return true;
  }
  function control(action, enabled) {
    if (disposed) return;
    if (action === 0) open = Boolean(enabled);
    else if (action === 1) hostPaused = Boolean(enabled);
    else if (action === 2) muted = Boolean(enabled);
    else return;
    clearQueue();
  }
  function visibility() { hidden = Boolean(documentObject?.hidden); clearQueue(); }
  const problem = message => onError(new Error(String(message)));
  module.onDaysAudio = receive;
  module.onDaysAudioControl = control;
  module.onDaysAudioProblem = problem;
  documentObject?.addEventListener?.('visibilitychange', visibility);
  return {
    async resume() {
      if (disposed) throw new Error('The Days audio sink has been disposed.');
      if (!AudioContextClass) throw new Error('This browser does not support WebAudio.');
      if (!context) {
        context = new AudioContextClass({latencyHint:'interactive'});
        gain = context.createGain(); gain.gain.value = volume; gain.connect(context.destination);
        context.addEventListener('statechange', stateChanged);
        module._days_web_audio_set_device_rate(context.sampleRate);
      }
      // This is deliberately the only place resume is called. The page must
      // invoke it from the player's gesture; receiving PCM never bypasses it.
      await context.resume();
      clearQueue(); stateChanged();
      return context.state === 'running';
    },
    setVolume(value) {
      if (!Number.isFinite(value)) throw new TypeError('Sound level must be a finite number.');
      volume = Math.max(0, Math.min(1, value));
      if (gain) gain.gain.value = volume;
    },
    setPaused(value) { userPaused = Boolean(value); clearQueue(); },
    get statistics() { return {acceptedBlocks, droppedBlocks, queuedSeconds:context ? Math.max(0,nextTime-context.currentTime) : 0}; },
    async dispose({ notifyRuntime = true } = {}) {
      if (disposed) return;
      disposed = true; clearQueue();
      documentObject?.removeEventListener?.('visibilitychange', visibility);
      if (module.onDaysAudio === receive) module.onDaysAudio = previous.audio;
      if (module.onDaysAudioControl === control) module.onDaysAudioControl = previous.control;
      if (module.onDaysAudioProblem === problem) module.onDaysAudioProblem = previous.problem;
      if (notifyRuntime) module._days_web_audio_set_device_rate(0);
      if (context) {
        context.removeEventListener('statechange', stateChanged);
        gain.disconnect();
        await context.close();
      }
    },
  };
}
