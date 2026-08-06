#!/bin/bash
# 8-way parallel paired gauntlet: games 0..1999 in 250-game chunks.
# Usage: gaunt8.sh <harness-binary> <outdir> [level=0]
# Game numbers are deterministic (colour+seed = f(gameNo)), so chunked
# results merge into the same paired set the 2x1000 convention produces.
BIN=$1; OUT=$2; LVL=${3:-0}; BASE=${4:-0}
[ -x "$BIN" ] && [ -n "$OUT" ] || { echo "usage: gaunt8.sh <binary> <outdir> [level]"; exit 1; }
mkdir -p "$OUT"
for i in 0 1 2 3 4 5 6 7; do
  d="$OUT/w$i"; mkdir -p "$d"
  ( cd "$d" && "$BIN" hunt 250 "$LVL" 99 $((BASE+i*250)) > results.txt 2> err.txt ) &
done
wait
cat "$OUT"/w*/results.txt | grep '^RESULT' | sort -n -k2 > "$OUT/results_merged.txt"
n=$(wc -l < "$OUT/results_merged.txt"); w=$(awk '$3==1{c++}END{print c+0}' "$OUT/results_merged.txt")
echo "GAUNT8_DONE games=$n wins=$w"
