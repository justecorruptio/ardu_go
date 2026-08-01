#!/bin/bash
# Cycle-accurate on-device think() benchmark.
# Compiles the AVR bench firmware, finds the marker symbol, and runs
# it in the vendored ProjectABE emulator. Usage: ./run.sh [thinks]
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
SKETCH="$HERE/../bench_avr"
OUT=/tmp/ardugo_avrbench
CLI="/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"
NM=~/Library/Arduino15/packages/arduino/tools/avr-gcc/7.3.0-atmel3.6.1-arduino7/bin/avr-nm

# --clean is NOT optional: the CLI build cache does not reliably
# invalidate through bench_avr's SYMLINKED sources, and a stale cached
# binary once produced a phantom -25.6% "win" (213M) that survived two
# re-runs before clean builds exposed it.
"$CLI" compile --clean --fqbn arduino:avr:leonardo --output-dir "$OUT" "$SKETCH" >/dev/null
ADDR=$("$NM" "$OUT/bench_avr.ino.elf" | grep -iw benchMark | cut -d' ' -f1)
ADDR=${ADDR: -4}   # low 16 bits = data-space address
echo "marker benchMark @ 0x$ADDR"
node "$HERE/avrbench.js" "$OUT/bench_avr.ino.hex" "$ADDR" "${1:-10}"
