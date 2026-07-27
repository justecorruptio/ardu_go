#!/bin/bash
# Verify game/ai stay clear of RAM 0x800-0x801 in the AVR build, and
# that the size flags (-mcall-prologues -mrelax, from the AVR core's
# platform.local.txt) are active — without them the sketch overflows
# flash, and a Boards Manager core update deletes that file silently.
B=${1:-/tmp/ardugo_magic_build}
CLI="/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"
"$CLI" compile --fqbn arduino:avr:leonardo --build-path "$B" \
    "$(dirname "$0")/.." > /dev/null 2>&1
if ! grep -aq -- "-mrelax" "$B/ardu_go.ino.elf"; then
    echo "*** SIZE FLAGS MISSING: restore platform.local.txt with"
    echo "*** '-mcall-prologues -mrelax' in the AVR core dir"
    exit 1
fi
NM=~/Library/Arduino15/packages/arduino/tools/avr-gcc/7.3.0-atmel3.6.1-arduino7/bin/avr-nm
$NM -S -td "$B/ardu_go.ino.elf" | awk '
  / [bBdD] (game|ai)$/ {
    addr = $1 % 8388608; size = $2 + 0
    printf "%s: 0x%X..0x%X", $4, addr, addr + size - 1
    if (addr <= 2049 && addr + size > 2048) { print "  *** SPANS 0x800 ***"; bad = 1 }
    else print "  ok"
  }
  END { exit bad }'
