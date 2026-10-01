/* UART JSONL validation; also loadable by the dependency-free Node tests. */
(function(root) {
  function decodeEvent(o) {
    if (!o || typeof o !== 'object' || Array.isArray(o)) return null;
    if (o.type === 'fft') {
      if (!Array.isArray(o.bins) || o.bins.length !== 128 ||
          !o.bins.every(v => Number.isSafeInteger(v) && v >= 0 && v <= 0xffffffffff)) return null;
      return {...o, bins: o.bins.slice()};
    }
    if (o.type === 'rx') {
      if (!Number.isInteger(o.data) || o.data < 0 || o.data > 255 || o.crc_ok !== true) return null;
      return {...o, char: String.fromCharCode(o.data)};
    }
    return ['recognition','capture','status'].includes(o.type) ? o : null;
  }
  function charLabel(ch) {
    if (ch === ' ') return '공백';
    if (ch === '\n') return 'LF';
    if (ch === '\r') return 'CR';
    if (ch === '\t') return 'TAB';
    if (ch && ch.charCodeAt(0) < 32) return `0x${ch.charCodeAt(0).toString(16).padStart(2,'0')}`;
    return ch || '—';
  }
  class Lines {
    constructor() { this.pending=''; this.dropping=false; }
    push(text) {
      const lines=[];
      for (const ch of text) {
        if (ch === '\n') {
          if (!this.dropping && this.pending.trim()) lines.push(this.pending.trim());
          this.pending=''; this.dropping=false;
        } else if (!this.dropping) {
          this.pending+=ch;
          if (this.pending.length > 8192) { this.pending=''; this.dropping=true; }
        }
      }
      return lines;
    }
  }
  const api={decodeEvent,charLabel,Lines};
  if (typeof module !== 'undefined' && module.exports) module.exports=api;
  else root.LightLetterProtocol=api;
})(typeof window !== 'undefined' ? window : globalThis);
