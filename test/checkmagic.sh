#!/bin/bash
# Verify all state that must not be corrupted stays clear of RAM
# 0x800-0x801 (the bootloader magic key, stompable by hardware/USB).
# Since the per-access 0x800 redirect was removed for speed, this
# build-time assertion IS the safety net — it must cover every object
# the engine indexes into:
#   game, ai            game state + book walker (halt-loud at runtime too)
#   floodScratch        flood-fill work stack (floodSlot indexes it raw)
#   poolExt             static node-pool extension (node() indexes it raw)
#   sBuffer node region first 858 B of the screen buffer = pool[0..142]
#                       (the RAVE tables ABOVE 858 B legitimately own
#                        0x800 — pure stats, a stomp is harmless noise)
# Also checks the size flags (-mcall-prologues -mrelax from the AVR
# core's platform.local.txt) are active — a Boards Manager core update
# deletes that file silently and the sketch then overflows flash.
B=${1:-/tmp/ardugo_magic_build}
CLI="/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"
"$CLI" compile --clean --fqbn arduino:avr:leonardo --build-path "$B" \
    "$(dirname "$0")/.." > /dev/null 2>&1
# node region = NODE_POOL_SB * sizeof(Node); read from source so this check
# can never go stale when the pool size changes.
# (two defines exist since the depth arc: the NORAVE=164 branch is a
# host-only experiment; the device build always takes the #else = the
# LAST match, and the worst case is the larger pool anyway)
NPSB=$(grep -oE '#define[[:space:]]+NODE_POOL_SB[[:space:]]+[0-9]+' "$(dirname "$0")/../ai.cpp" | grep -oE '[0-9]+$' | tail -1)
NODEREGION=$(( NPSB * 6 ))
if ! grep -aq -- "-mrelax" "$B/ardu_go.ino.elf"; then
    echo "*** SIZE FLAGS MISSING: restore platform.local.txt with"
    echo "*** '-mcall-prologues -mrelax -flto' in the AVR core dir"
    echo "*** (canonical copy: test/platform.local.txt)"
    exit 1
fi
# LTO fingerprint: an LTO link's producer is "GNU GIMPLE", not "GNU C++"
if ! grep -aq "GNU GIMPLE" "$B/ardu_go.ino.elf"; then
    echo "*** -flto MISSING (costs +302 B): restore platform.local.txt"
    echo "*** from test/platform.local.txt into the AVR core dir"
    exit 1
fi
NM=~/Library/Arduino15/packages/arduino/tools/avr-gcc/7.3.0-atmel3.6.1-arduino7/bin/avr-nm
# Node = 6 bytes, NODE_POOL_SB = 143 -> node region = 858 bytes.
$NM -S -td "$B/ardu_go.ino.elf" | awk -v nr="$NODEREGION" '
  function check(name, addr, size,   last) {
    last = addr + size - 1
    printf "%-16s 0x%X..0x%X", name, addr, last
    if (addr <= 2049 && last >= 2048) { print "  *** SPANS 0x800 ***"; bad = 1 }
    else print "  ok"
  }
  / [bBdD] (game|ai|floodScratch)$/ { check($4, $1 % 8388608, $2 + 0) }
  # boardAt() (ai.cpp) builds &simBoard[q] with a constant high byte:
  # lo8(simBoard) + 80 must not carry or every flood loop reads the
  # wrong page. Assert lo8 <= 0xAF.
  / [bBdD] _ZL7simMark$/ {
    lo = ($1 % 8388608) % 256
    # markPtr carry-FREE again (2026-08-15 asm audit): assert like boardAt.
    printf "%-16s lo8=0x%X", "simMark", lo
    if (lo > 175) { print "  *** markPtr CARRY: lo8(simMark)+80 > 0xFF ***"; bad = 1 }
    else print "  ok (markPtr carry-free)"
    seen_simmark = 1
  }
  / [bBdD] _ZL8simBoard$/ {
    lo = ($1 % 8388608) % 256
    printf "%-16s lo8=0x%X", "simBoard", lo
    if (lo > 175) { print "  *** boardAt CARRY: lo8(simBoard)+80 > 0xFF ***"; bad = 1 }
    else print "  ok (boardAt carry-free)"
    seen_simboard = 1
  }
  / [bBdD] _ZL7chainId$/ {
    lo = ($1 % 8388608) % 256
    # chainPtr is now carry-CORRECT (ldi 0 / subi / sbci), layout-independent.
    printf "%-16s lo8=0x%X  ok (chainPtr carry-correct)\n", "chainId", lo
    seen_chainid = 1
  }
  / [bBdD] _ZL7poolExt$/           { check("poolExt", $1 % 8388608, $2 + 0) }
  / [bBdD] _ZN12Arduboy2Base7sBufferE$/ {
    check("pool(nodes)", $1 % 8388608, nr+0)   # node region NODE_POOL_SB*6; RAVE above owns 0x800
    seen_sbuf = 1
  }
  END {
    if (!seen_sbuf) { print "*** sBuffer symbol not found ***"; bad = 1 }
    if (!seen_simboard) { print "*** simBoard symbol not found ***"; bad = 1 }
    if (!seen_simmark) { print "*** simMark symbol not found ***"; bad = 1 }
    if (!seen_chainid) { print "*** chainId symbol not found ***"; bad = 1 }
    exit bad
  }'
