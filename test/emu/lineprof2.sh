#!/bin/bash
# Trustworthy per-LINE think() profile, layered on the call-stack tracker.
#   ./lineprof2.sh [iters]     (default: shipped MCTS_ITERATIONS=400)
# lineprof2.js reuses avrprof's shadow-stack (CALL/RET) so the LEAF function
# is authoritative even under -flto; it buckets (leaf, block-PC). Per-function
# totals therefore match avrprof EXACTLY (verified below), and the PC gives the
# hot line within each real out-of-line function. PC is a WORD addr -> byte is
# pc<<1 for addr2line. Unlike the raw PC-histogram lineprof.js (which buckets
# raw PCs that land ~95% in the LTO-fused blob and does NOT reconcile), this
# one is sound.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"; SK="$HERE/../bench_avr"; OUT=/tmp/ardugo_line2
CLI="/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"
BIN=~/Library/Arduino15/packages/arduino/tools/avr-gcc/7.3.0-atmel3.6.1-arduino7/bin
NM="$BIN/avr-nm"; A2L="$BIN/avr-addr2line"
ITERS="${1:-}"
PROP="--build-property compiler.cpp.extra_flags=-DBENCH_PAD=${BENCH_PAD:-12}"; [ -n "$ITERS" ] && PROP="--build-property compiler.cpp.extra_flags=-DBENCH_PAD=${BENCH_PAD:-12} -DMCTS_ITERATIONS=$ITERS"
"$CLI" compile --fqbn arduino:avr:leonardo $PROP --output-dir "$OUT" "$SK" >/dev/null
ELF="$OUT/bench_avr.ino.elf"
"$NM" -CnS --defined-only "$ELF" > "$OUT/sym.nm"
MARK=$("$NM" "$ELF" | grep -iw benchMark | awk '{print $1}'); MARK=${MARK: -4}
node "$HERE/lineprof2.js" "$OUT/bench_avr.ino.hex" "$OUT/sym.nm" "$MARK" > "$OUT/lp2.txt" 2>/dev/null
node "$HERE/avrprof.js"  "$OUT/bench_avr.ino.hex" "$OUT/sym.nm" "$MARK" > "$OUT/ap.folded" 2>/dev/null
python3 - "$OUT/lp2.txt" "$OUT/ap.folded" "$ELF" "$A2L" "$SK" <<'PY'
import sys,subprocess,collections,re
lp,ap,elf,a2l,sk=sys.argv[1:6]; srcdir=sk.rstrip('/')+'/'
rows=[(int(p[0]),int(p[1]),p[2].split('(')[0]) for p in
      (l.rstrip('\n').split(' ',2) for l in open(lp)) if len(p)==3]
tot=sum(c for _,c,_ in rows)
# VERIFY: per-function totals vs avrprof self-time
a=collections.Counter(); at=0
for l in open(ap):
    i=l.rfind(' '); c=int(l[i+1:]); at+=c; a[l[:i].split(';')[-1].split('(')[0]]+=c
b=collections.Counter()
for _,c,leaf in rows: b[leaf]+=c
worst=max(abs(100*a[n]/at-100*b[n]/tot) for n in a)
print(f"[verify] max per-fn deviation vs avrprof: {worst:.2f}%  ({'OK' if worst<1 else 'MISMATCH'})\n")
# resolve PCs (word<<1 = byte) to lines
pcs=sorted({pc for pc,_,_ in rows})
o=subprocess.run([a2l,"-fC","-e",elf,*[f"0x{pc*2:x}" for pc in pcs]],capture_output=True,text=True).stdout.splitlines()
loc={pc:(o[2*i+1].strip().split('/')[-1] if 2*i+1<len(o) else '?') for i,pc in enumerate(pcs)}
byleaf=collections.Counter(); byline=collections.defaultdict(collections.Counter)
for pc,c,leaf in rows: byleaf[leaf]+=c; byline[leaf][loc[pc]]+=c
src={f:(open(srcdir+f).read().splitlines() if __import__('os').path.exists(srcdir+f) else []) for f in ('ai.cpp','game.cpp')}
def txt(l):
    m=re.match(r'(ai|game)\.cpp:(\d+)',l);  n=int(m.group(2)) if m else 0
    return src[m.group(1)+'.cpp'][n-1].strip()[:52] if m and n-1<len(src[m.group(1)+'.cpp']) else ''
print(f"total think {tot} cyc — hot lines within each top function:\n")
for leaf,_ in byleaf.most_common(10):
    if leaf in ('setup','???'): continue
    print(f"* {leaf}  ({100*byleaf[leaf]/tot:.1f}% of move)")
    for l,c in byline[leaf].most_common(5):
        if c/tot<0.004: break
        print(f"    {100*c/tot:5.2f}%  {l:15} {txt(l)}")
PY
