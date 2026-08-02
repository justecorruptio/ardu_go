// Call-stack-based per-LINE profiler. Same shadow-stack tracker as
// avrprof.js (watch CALL/RET, know the current leaf function every block),
// but buckets (leafFunction, block-entry PC) instead of the full stack.
// Because the leaf comes from call/return tracking — not from resolving the
// fused PC — the per-FUNCTION totals match avrprof exactly, while the PC
// gives the hot line WITHIN each (real, out-of-line) function.
//
//   node lineprof2.js <firmware.hex> <symbols.nm> <markerHexAddr>
//   -> emits "<pcWord> <cycles> <leafName>" per (leaf,pc) bucket
'use strict';
global.window = global;
global.self = global;
if (!global.performance) global.performance = { now: () => Date.now() };
const _log = console.log, _err = console.error;
console.log = () => {};

const fs = require('fs');
const path = require('path');
const Atcore = require(path.join(__dirname, 'atcore', 'Atcore.js'));

const HEX = process.argv[2], NMFILE = process.argv[3];
const MARK = parseInt(process.argv[4], 16);

const funcEntry = new Map();     // wordAddr -> name
const ranges = [];               // {lo, hi, name} word addresses, sorted
for (const line of fs.readFileSync(NMFILE, 'utf8').split('\n')) {
  const m = line.match(/^([0-9a-f]+)\s+([0-9a-f]+)\s+([tTwW])\s+(.+)$/);
  if (!m) continue;
  const addr = parseInt(m[1], 16) >> 1;      // byte -> word
  const size = Math.max(1, parseInt(m[2], 16) >> 1);
  funcEntry.set(addr, m[4]);
  ranges.push({ lo: addr, hi: addr + size, name: m[4] });
}
ranges.sort((a, b) => a.lo - b.lo);
function funcOf(pc) {
  let lo = 0, hi = ranges.length - 1, best = '???';
  while (lo <= hi) {
    const mid = (lo + hi) >> 1;
    if (ranges[mid].lo <= pc) { if (pc < ranges[mid].hi) best = ranges[mid].name; lo = mid + 1; }
    else hi = mid - 1;
  }
  return best;
}

const core = Atcore.ATmega32u4();
(function loadHex() {
  let base = 0;
  for (const line of fs.readFileSync(HEX, 'utf8').split(/\r?\n/)) {
    if (line[0] !== ':') continue;
    const len = parseInt(line.substr(1, 2), 16), addr = parseInt(line.substr(3, 4), 16),
          type = parseInt(line.substr(7, 2), 16);
    if (type === 4) { base = parseInt(line.substr(9, 4), 16) << 16; continue; }
    if (type !== 0) continue;
    for (let i = 0; i < len; i++) core.flash[base + addr + i] = parseInt(line.substr(9 + i * 2, 2), 16);
  }
})();

let profiling = 0, done = 0;
core.writeMap[MARK] = (v) => {
  v &= 0xff;
  if (v === 0xA0) profiling = 1;
  else if (v === 0xA1 && profiling) { profiling = 0; done = 1; }
};

const stack = [{ name: funcOf(core.pc), ret: -1 }];
const buckets = new Map();        // "pcWord\tleaf" -> cycles
const hits = new Map();           // block entries per bucket
const mem = core.memory;

const BIG = 1 << 30;
let guard = 0;
while (!done && guard++ < 3e9) {
  const prevPC = core.pc, prevSP = core.sp, t0 = core.tick;
  core.endTick = core.tick + BIG;
  const f = core.native[prevPC];
  if (f) f.call(core);
  else if (core.getBlock() === false) break;
  const dt = core.tick - t0;

  if (profiling && dt > 0) {
    // leaf function = top of the shadow stack (authoritative, from CALL/RET);
    // prevPC = the block-entry PC whose cost we're charging this iteration.
    const leaf = stack[stack.length - 1].name;
    const key = prevPC + '\t' + leaf;
    buckets.set(key, (buckets.get(key) || 0) + dt);
    hits.set(key, (hits.get(key) || 0) + 1);
  }

  const newPC = core.pc, newSP = core.sp;
  if (newSP < prevSP && funcEntry.has(newPC)) {          // CALL
    const ret = mem[newSP + 1] | (mem[newSP + 2] << 8);
    stack.push({ name: funcEntry.get(newPC), ret });
  } else {                                                // maybe RET(s)
    while (stack.length > 1 && newPC === stack[stack.length - 1].ret &&
           newSP > prevSP) stack.pop();
  }
}

for (const [k, c] of [...buckets.entries()].sort((a, b) => b[1] - a[1])) {
  const [pc, leaf] = k.split('\t');
  _log(`${pc} ${c} ${hits.get(k)} ${leaf}`);
}
_err(`blocks profiled into ${buckets.size} (pc,leaf) buckets; guard=${guard}`);
