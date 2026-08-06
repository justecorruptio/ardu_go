#!/bin/bash
# Parallel (paired) gauntlet vs GnuGo -- ALWAYS use this, not a sequential
# `./harness N level` run. Shards games across W worker processes via the
# harness `hunt` mode, which prints "RESULT <gameNo> <win>" per game. gameNo
# fully determines colour + RNG seed (Knuth-hash), so the W chunks merge into
# exactly the paired set a single run would produce -- ~Wx faster.
#
#   ./pgauntlet.sh <binaryA>              # one arm, N games
#   ./pgauntlet.sh <binaryA> <binaryB>    # paired A vs B (McNemar)
#
# Env: N total games/arm (default 2000), W workers (default 8, = physical
#      cores), LVL gnugo level (default 0 -- the referee is ALWAYS L0).
set -e
A="$1"; B="$2"
N=${N:-2000}; W=${W:-8}; LVL=${LVL:-0}
[ -x "$A" ] || { echo "usage: pgauntlet.sh <binaryA> [binaryB]   (env: N W LVL)"; exit 1; }
TMP=$(mktemp -d /tmp/pgaunt.XXXXXX)
CHUNK=$(( (N + W - 1) / W ))

run_arm() {  # <binary> <outdir> -> writes $outdir/merged.txt (RESULT gameNo win)
  local bin="$1" out="$2" i d
  mkdir -p "$out"
  for i in $(seq 0 $((W-1))); do
    d="$out/w$i"; mkdir -p "$d"
    ( cd "$d" && "$bin" hunt $CHUNK "$LVL" 99 $((i*CHUNK)) >results.txt 2>err.txt ) &
  done
  wait
  cat "$out"/w*/results.txt | grep '^RESULT' | sort -n -k2 -u | head -n $N > "$out/merged.txt"
}

echo "arm A: $(basename "$A")  ($N games, $W workers, L$LVL)"
t0=$(date +%s); run_arm "$A" "$TMP/A"; echo "  A done in $(( $(date +%s)-t0 ))s"
if [ -n "$B" ]; then
  echo "arm B: $(basename "$B")"
  t1=$(date +%s); run_arm "$B" "$TMP/B"; echo "  B done in $(( $(date +%s)-t1 ))s"
fi

python3 - "$TMP/A/merged.txt" "${B:+$TMP/B/merged.txt}" <<'PY'
import sys
def load(p):
    d={}
    for l in open(p):
        _,g,w=l.split(); d[int(g)]=int(w)
    return d
a=load(sys.argv[1]); n=len(a); aw=sum(a.values())
print(f"A: {aw}/{n} = {100*aw/n:.1f}% vs gnugo L0")
if len(sys.argv)>2 and sys.argv[2]:
    b=load(sys.argv[2]); c=sorted(set(a)&set(b)); m=len(c)
    bw=sum(b[g] for g in c); awc=sum(a[g] for g in c)
    aonly=sum(1 for g in c if a[g] and not b[g]); bonly=sum(1 for g in c if b[g] and not a[g])
    print(f"B: {bw}/{len(b)} = {100*bw/len(b):.1f}%")
    print(f"paired {m}: A {awc} vs B {bw}  delta {bw-awc:+d} ({100*(bw-awc)/m:+.1f}%)")
    print(f"discordant {aonly+bonly}: B-only wins {bonly}, A-only wins {aonly}, net {bonly-aonly:+d}")
    import math
    if aonly+bonly: print(f"McNemar chi^2 ~ {(abs(bonly-aonly)-1)**2/(aonly+bonly):.1f} (>3.8 => p<0.05)")
PY
echo "(work: $TMP)"
