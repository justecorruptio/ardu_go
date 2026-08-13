#!/bin/bash
run() { # $1=binary $2=outroot
  mkdir -p "$2"
  for i in $(seq 0 7); do d="$2/w$i"; mkdir -p "$d"
    ( cd "$d" && "$1" 125 0 0 1000 1 2 $((9000 + i*125)) >out.txt 2>err.txt ) &
  done; wait
  grep -ho "AI WIN" "$2"/w*/out.txt | wc -l
}
echo "=== L0 fresh pair start $(date '+%H:%M:%S') ==="
S=$(run /tmp/hbin_ship /tmp/l0f_ship)
echo "[L0 FRESH ship n=1000 seeds9000] $S"
C=$(run /tmp/hbin_r4s0 /tmp/l0f_r4s0)
echo "[L0 FRESH r4s0 n=1000 seeds9000] $C"
echo "=== L0 FRESH DONE $(date '+%H:%M:%S') ==="
