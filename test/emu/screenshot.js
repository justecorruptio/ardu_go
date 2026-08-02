// Screen capture from the vendored emulator core: run the firmware for
// a fixed emulated time, then dump the Arduboy screen buffer (sBuffer)
// from RAM as a PBM bitmap on stdout. Layout iteration without hardware.
//
//   node screenshot.js <firmware.hex> <sBufferHexAddr> [seconds] > out.pbm
//
// sBuffer is 1024 bytes, 128x64, column-major pages: byte = 8 vertical
// pixels, page p row = p*8 + bit. The sketch runs jay.invert(1), which
// the SSD1306 applies at the panel, not in sBuffer -- pass INVERT=1 in
// the env to match what the OLED shows.
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
const SBUF = parseInt(process.argv[3], 16);
const SECS = parseFloat(process.argv[4] || '2');
if (!HEX || isNaN(SBUF)) {
  _log('usage: node screenshot.js <firmware.hex> <sBufferHexAddr> [seconds]');
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
// Arduboy buttons are active-low; the emulator's unwired ports read 0
// (= everything pressed), which traps boot in flashlight()'s
// while(pressed(UP)) idle() loop. Pull the button ports high:
// PINF (D-pad, data 0x2F), PINE (A, 0x2C), PINB (B, 0x23).
for (const pin of [0x2f, 0x2c, 0x23]) core.readMap[pin] = () => 0xff;
for (let t = 0; t < SECS / 0.1; t++) core.exec(0.1);
// sBuffer is cleared and redrawn every frame, so a blind capture races
// the renderer. Sync to the frame FLUSH instead: display() streams the
// buffer out over SPI, so the first SPDR write (data addr 0x4E) marks
// a complete, freshly-drawn buffer.
let flushed = false;
core.writeMap[0x4e] = () => { flushed = true; };
let guard = 0;
while (!flushed && guard++ < 4000) core.exec(0.0001);
if (!flushed) process.stderr.write('warning: no SPI flush seen; buffer may be torn\n');

// SRAM starts at data address 0x100; core.sram covers it from index 0
// unless the array includes the register file -- probe both mappings
// and pick the one whose buffer isn't all zero.
function grab(off) {
  const out = Buffer.alloc(1024);
  for (let i = 0; i < 1024; i++) out[i] = core.sram[off + i] & 0xff;
  return out;
}
let buf = grab(SBUF - 0x100);
if (!buf.some(b => b)) buf = grab(SBUF);

const inv = process.env.INVERT === '1';
const rows = [];
for (let y = 0; y < 64; y++) {
  let row = '';
  for (let x = 0; x < 128; x++) {
    let on = (buf[(y >> 3) * 128 + x] >> (y & 7)) & 1;
    if (inv) on ^= 1;
    row += on ? '1' : '0';
  }
  rows.push(row);
}
process.stdout.write('P1\n128 64\n' + rows.join('\n') + '\n');
