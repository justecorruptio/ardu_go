// Value-identity check: run bench firmware, at each think-end marker
// hash the borrowed-sBuffer node pool + search counters. Deterministic
// seed => hashes must match across compiler-flag variants.
'use strict';
global.window = global; global.self = global;
if (!global.performance) global.performance = { now: () => Date.now() };
const _log = console.log; console.log = () => {};
const fs = require('fs'); const path = require('path');
const Atcore = require(path.join(process.argv[5], 'Atcore.js'));
const HEX = process.argv[2], MARK = parseInt(process.argv[3], 16), SBUF = parseInt(process.argv[4], 16);
function loadHex(core, file) {
  let base = 0;
  for (const line of fs.readFileSync(file, 'utf8').split(/\r?\n/)) {
    if (line[0] !== ':') continue;
    const len = parseInt(line.substr(1, 2), 16), addr = parseInt(line.substr(3, 4), 16), type = parseInt(line.substr(7, 2), 16);
    if (type === 0x04) { base = parseInt(line.substr(9, 4), 16) << 16; continue; }
    if (type === 0x02) { base = parseInt(line.substr(9, 4), 16) << 4; continue; }
    if (type !== 0x00) continue;
    for (let i = 0; i < len; i++) core.flash[base + addr + i] = parseInt(line.substr(9 + i * 2, 2), 16);
  }
}
const core = Atcore.ATmega32u4();
loadHex(core, HEX);
let done = 0;
core.writeMap[MARK] = (v) => {
  if ((v & 0xff) === 0xA1) {
    const crypto = require('crypto');
    const h = crypto.createHash('md5');
    h.update(Buffer.from(core.memory.slice(SBUF, SBUF + 1024)));
    _log('POOLHASH ' + h.digest('hex'));
    if (++done >= (parseInt(process.argv[6]) || 2)) process.exit(0);
  }
};
for (let t = 0; t < 3000; t++) core.exec(0.1);
_log('TIMEOUT');
