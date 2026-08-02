#!/bin/bash
# One-command score-screen iteration: build the demo sketch, run it in
# the emulator, save + open a PNG of the screen. Usage: ./shot.sh
set -e
CLI="/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"
NM=~/Library/Arduino15/packages/arduino/tools/avr-gcc/7.3.0-atmel3.6.1-arduino7/bin/avr-nm
cd /Users/jay/workspace/ardu_go
"$CLI" compile --fqbn arduino:avr:leonardo --output-dir /tmp/score_demo . >/dev/null
SB=$($NM /tmp/score_demo/ardu_go.ino.elf | grep sBufferE | awk '{print $1}')
DP=$($NM /tmp/score_demo/ardu_go.ino.elf | grep "7displayEv" | awk '{print $1}')
INVERT=1 DISPLAY_PC=0x${DP: -4} node test/emu/screenshot.js /tmp/score_demo/ardu_go.ino.hex 0x${SB: -4} 6 > /tmp/score.pbm
python3 - <<'PY'
from PIL import Image
rows = [l for l in open('/tmp/score.pbm').read().split('\n')[2:] if l]
S = 4
img = Image.new('L', (128*S, 64*S))
px = img.load()
for y, r in enumerate(rows):
    for x, c in enumerate(r):
        v = 255 if c == '1' else 25
        for dy in range(S):
            for dx in range(S):
                px[x*S+dx, y*S+dy] = v
img.save('/tmp/score_screen.png')
PY
open /tmp/score_screen.png
