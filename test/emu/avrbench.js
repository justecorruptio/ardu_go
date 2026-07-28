// Cycle-accurate think() benchmark. Runs an AVR firmware in the
// vendored ProjectABE emulator core and measures the clock cycles
// between writes to a marker variable (0xA0 = start, 0xA1 = end),
// giving true on-device timing that host benchmarks cannot.
//
//   node avrbench.js <firmware.hex> <markerHexAddr> [thinks]
//
// Validated against hand-computed AVR timing: a volatile-uint32 loop
// measures 30.2 cyc/iter, a software 32-bit divide 662 cyc — both
// within 1% of documented costs.
'use strict';
global.window = global;
global.self = global;
if (!global.performance) global.performance = { now: () => Date.now() };
const _log = console.log;
console.log = () => {};   // mute emulator peripheral chatter

const fs = require('fs');
const path = require('path');
const Atcore = require(path.join(__dirname, 'atcore', 'Atcore.js'));

const HEX = process.argv[2];
const MARK = parseInt(process.argv[3], 16);
const N = parseInt(process.argv[4] || '20');
const CLOCK = 16e6;
if (!HEX || isNaN(MARK)) {
  _log('usage: node avrbench.js <firmware.hex> <markerHexAddr> [thinks]');
  process.exit(1);
}

function loadHex(core, file) {
  let base = 0;
  for (const line of fs.readFileSync(file, 'utf8').split(/\r?\n/)) {
    if (line[0] !== ':') continue;
    const len = parseInt(line.substr(1, 2), 16);
    const addr = parseInt(line.substr(3, 4), 16);
    const type = parseInt(line.substr(7, 2), 16);
    if (type === 0x04) { base = parseInt(line.substr(9, 4), 16) << 16; continue; }
    if (type === 0x02) { base = parseInt(line.substr(9, 4), 16) << 4; continue; }
    if (type !== 0x00) continue;
    for (let i = 0; i < len; i++)
      core.flash[base + addr + i] = parseInt(line.substr(9 + i * 2, 2), 16);
  }
}

const core = Atcore.ATmega32u4();
loadHex(core, HEX);

const marks = [];
core.writeMap[MARK] = (v) => { marks.push({ v: v & 0xff, tick: core.tick }); };

const MAX_SEC = 300;      // emulated-time budget (device thinks are seconds)
const intervals = [];
let lastStart = null;
for (let t = 0; t < MAX_SEC / 0.1; t++) {
  core.exec(0.1);
  while (marks.length) {
    const m = marks.shift();
    if (m.v === 0xA0) lastStart = m.tick;
    else if (m.v === 0xA1 && lastStart !== null) {
      intervals.push(m.tick - lastStart);
      lastStart = null;
      if (intervals.length >= N) { report(); process.exit(0); }
    }
  }
}
if (!intervals.length) { _log('no thinks measured — boot may have hung'); process.exit(1); }
report();

function report() {
  intervals.sort((a, b) => a - b);
  const min = intervals[0], max = intervals[intervals.length - 1];
  const med = intervals[intervals.length >> 1];
  const mean = intervals.reduce((a, b) => a + b, 0) / intervals.length | 0;
  const s = c => (c / CLOCK).toFixed(3);
  _log(`thinks=${intervals.length}`);
  _log(`cycles/think  min=${min}  median=${med}  mean=${mean}  max=${max}`);
  _log(`sec/think@16MHz  min=${s(min)}  median=${s(med)}  mean=${s(mean)}  max=${s(max)}`);
}
