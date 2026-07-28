// Cycle-exact call-stack profiler for AVR firmware in the ProjectABE
// core. Runs block-by-block, maintains a shadow call stack by watching
// CALL (pc -> function entry, SP drops) and RET (pc == saved return
// address), and attributes each basic block's exact cycle cost to the
// current stack. Emits Brendan-Gregg "collapsed stacks" between the
// 0xA0/0xA1 marker writes bracketing one think().
//
//   node avrprof.js <firmware.hex> <symbols.nm> <markerHexAddr> > out.folded
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

// --- symbols: build word-address -> function name, and a sorted range
//     table for leaf lookup. avr-nm -nS lines: "addr size type name" ---
const funcEntry = new Map();     // wordAddr -> name
const ranges = [];               // {lo, hi, name} word addresses, sorted
for (const line of fs.readFileSync(NMFILE, 'utf8').split('\n')) {
  const m = line.match(/^([0-9a-f]+)\s+([0-9a-f]+)\s+([tTwW])\s+(.+)$/);
  if (!m) continue;
  const addr = parseInt(m[1], 16) >> 1;      // byte -> word
  const size = Math.max(1, parseInt(m[2], 16) >> 1);
  const name = m[4];
  funcEntry.set(addr, name);
  ranges.push({ lo: addr, hi: addr + size, name });
}
ranges.sort((a, b) => a.lo - b.lo);
function funcOf(pc) {                          // binary search
  let lo = 0, hi = ranges.length - 1, best = '???';
  while (lo <= hi) {
    const mid = (lo + hi) >> 1;
    if (ranges[mid].lo <= pc) {
      if (pc < ranges[mid].hi) { best = ranges[mid].name; }
      lo = mid + 1;
    } else hi = mid - 1;
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

// shadow stack of {name, ret(wordAddr)}; buckets: stackKey -> cycles
const stack = [{ name: funcOf(core.pc), ret: -1 }];
const buckets = new Map();
const mem = core.memory;

const BIG = 1 << 30;
let guard = 0;
while (!done && guard++ < 3e9) {
  const prevPC = core.pc, prevSP = core.sp, t0 = core.tick;
  core.endTick = core.tick + BIG;          // let the block run to its natural end
  const f = core.native[prevPC];
  if (f) f.call(core);
  else if (core.getBlock() === false) break;
  const dt = core.tick - t0;

  if (profiling && dt > 0) {
    const key = stack.map(s => s.name).join(';');
    buckets.set(key, (buckets.get(key) || 0) + dt);
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

for (const [k, c] of [...buckets.entries()].sort((a, b) => b[1] - a[1]))
  _log(`${k} ${c}`);
_err(`blocks profiled into ${buckets.size} stacks; guard=${guard}`);
