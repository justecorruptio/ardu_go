#!/bin/bash
# Cycle-exact call-stack profile of think(), emitted as Brendan-Gregg
# "collapsed stacks" (flamegraph.pl / inferno compatible) on stdout.
# Uses reduced iterations for speed; proportions match a full move.
#   ./prof.sh [iterations] > out.folded
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"; SK="$HERE/../bench_avr"; OUT=/tmp/ardugo_prof
CLI="/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"
NM=~/Library/Arduino15/packages/arduino/tools/avr-gcc/7.3.0-atmel3.6.1-arduino7/bin/avr-nm
ITERS=${1:-40}
"$CLI" compile --fqbn arduino:avr:leonardo \
  --build-property "compiler.cpp.extra_flags=-mcall-prologues -mrelax -DMCTS_ITERATIONS=$ITERS" \
  --output-dir "$OUT" "$SK" >/dev/null
"$NM" -CnS --defined-only "$OUT/bench_avr.ino.elf" > "$OUT/sym.nm"
A=$("$NM" "$OUT/bench_avr.ino.elf" | grep -iw benchMark | cut -d' ' -f1); A=${A: -4}
node "$HERE/avrprof.js" "$OUT/bench_avr.ino.hex" "$OUT/sym.nm" "$A"
