#!/bin/bash
run() { mkdir -p "$2"
  for i in $(seq 0 7); do d="$2/w$i"; mkdir -p "$d"
    ( cd "$d" && "$1" 125 0 0 1000 1 2 $(($3 + i*125)) >out.txt 2>err.txt ) &
  done; wait
  grep -ho "AI WIN" "$2"/w*/out.txt | wc -l
}
for SB in 9000 11000 13000; do
  C=$(run /tmp/hbin_dg2s0 /tmp/l0f_r2s0_$SB $SB)
  echo "[L0 FRESH r2s0 n=1000 seeds$SB] $C  (ship same seeds: see l0fresh logs)"
done
echo "=== R2S0 L0 FRESH DONE $(date '+%H:%M:%S') ==="
