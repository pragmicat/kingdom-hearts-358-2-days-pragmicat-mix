export class FakeAudioContext {
  static instances = [];
  sampleRate = 48000; currentTime = 10; state = 'suspended'; destination = {};
  buffers = []; sources = []; listeners = new Set(); resumeCalls = 0; closeCalls = 0;
  constructor(options) { this.options = options; FakeAudioContext.instances.push(this); }
  createGain() { return {gain:{value:1},connect(){},disconnect(){}}; }
  createBuffer(channels, frames, rate) {
    const data = Array.from({length:channels},()=>new Float32Array(frames));
    const buffer = {frames,rate,getChannelData:side=>data[side]};
    this.buffers.push(buffer); return buffer;
  }
  createBufferSource() {
    const source = {connect(){},disconnect(){this.disconnected=true;},start(when){this.when=when;},stop(){this.stopped=true;}};
    this.sources.push(source); return source;
  }
  addEventListener(_,listener) { this.listeners.add(listener); }
  removeEventListener(_,listener) { this.listeners.delete(listener); }
  async resume() { this.resumeCalls++; this.state='running'; }
  async close() { this.closeCalls++; this.state='closed'; }
  changeState(state) { this.state=state; for(const listener of this.listeners) listener(); }
}
export class FakeDocument {
  hidden = false; listeners = new Set();
  addEventListener(_,fn){this.listeners.add(fn);}
  removeEventListener(_,fn){this.listeners.delete(fn);}
  hide(hidden){this.hidden=hidden;for(const fn of this.listeners)fn();}
}
