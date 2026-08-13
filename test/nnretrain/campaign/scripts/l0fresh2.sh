#!/bin/bash
run() { # $1=binary $2=outroot $3=seedbase
  mkdir -p "$2"
  for i in $(seq 0 7); do d="$2/w$i"; mkdir -p "$d"
    ( cd "$d" && "$1" 125 0 0 1000 1 2 $(($3 + i*125)) >out.txt 2>err.txt ) &
  done; wait
  grep -ho "AI WIN" "$2"/w*/out.txt | wc -l
}
for SB in 11000 13000; do
  echo "=== seedset $SB start $(date '+%H:%M:%S') ==="
  S=$(run /tmp/hbin_ship /tmp/l0f_ship_$SB $SB)
  echo "[L0 FRESH ship n=1000 seeds$SB] $S"
  C=$(run /tmp/hbin_r4s0 /tmp/l0f_r4s0_$SB $SB)
  echo "[L0 FRESH r4s0 n=1000 seeds$SB] $C"
done
echo "=== L0 FRESH2 DONE $(date '+%H:%M:%S') ==="
