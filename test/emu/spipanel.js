// SSD1306 panel-side screen capture: models the display controller from
// the emulator's actual SPI byte stream + D/C pin, so it shows what the
// PANEL shows — including writes that bypass sBuffer entirely (the
// think-progress bar blits straight to the controller while sBuffer is
// the MCTS node pool). screenshot.js (RAM dump) cannot see those.
//
//   node spipanel.js <firmware.hex> [seconds] > out.pbm
//
// Models: column window (0x21), page window (0x22), horizontal
// addressing with wrap inside the window. Ignores other commands.
'use strict';
global.window = global;
global.self = global;
if (!global.performance) global.performance = { now: () => Date.now() };
const _log = console.log;
console.log = () => {};

const fs = require('fs');
const path = require('path');
const Atcore = require(path.join(__dirname, 'atcore', 'Atcore.js'));

const HEX = process.argv[2];
const SECS = parseFloat(process.argv[3] || '8');
if (!HEX) { _log('usage: node spipanel.js <firmware.hex> [seconds]'); process.exit(1); }

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
core.pins.spiOut = core.pins.spiOut || [];

// Interleave D/C transitions (PORTD bit 4 on Arduboy) into the SPI stream
// as tagged markers so ordering vs data bytes is preserved.
const PORTD = 0x2B;
const prev = core.writeMap[PORTD];
core.writeMap[PORTD] = (v, old) => {
  core.pins.spiOut.push({ dc: (v >> 4) & 1 });
  return prev ? prev(v, old) : undefined;
};

// SSD1306 model
const panel = new Uint8Array(1024);          // [page*128 + col]
let dc = 0;
let c0 = 0, c1 = 127, p0 = 0, p1 = 7;        // window
let col = 0, page = 0;                       // pointer
let pend = null, args = [];
function feed(b) {
  if (typeof b === 'object') { dc = b.dc; return; }
  if (!dc) {                                  // command byte
    if (pend !== null) {
      args.push(b);
      if (pend === 0x21 && args.length === 2) { c0 = args[0]; c1 = args[1]; col = c0; page = p0; pend = null; }
      else if (pend === 0x22 && args.length === 2) { p0 = args[0]; p1 = args[1]; col = c0; page = p0; pend = null; }
      return;
    }
    if (b === 0x21 || b === 0x22) { pend = b; args = []; }
    return;
  }
  pend = null;
  panel[page * 128 + col] = b;                // data byte
  if (++col > c1) { col = c0; if (++page > p1) page = p0; }
}

const STEP = 0.05;
for (let t = 0; t < SECS; t += STEP) {
  core.exec(STEP);
  const q = core.pins.spiOut;
  while (q.length) feed(q.shift());
}

// PBM out (INVERT=1 env matches jay.invert(1) panel inversion)
const inv = process.env.INVERT === '1';
_log('P1'); _log('128 64');
for (let y = 0; y < 64; y++) {
  let row = '';
  for (let x = 0; x < 128; x++) {
    let bit = (panel[(y >> 3) * 128 + x] >> (y & 7)) & 1;
    if (inv) bit ^= 1;
    row += bit ? '1 ' : '0 ';
  }
  _log(row);
}
