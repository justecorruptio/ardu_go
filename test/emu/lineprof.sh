#!/bin/bash
# Per-source-LINE think() profile.  Companion to lineprof.js.
#   ./lineprof.sh [iters]
# Method: canonical bench build (same as run.sh: -g, -flto) so the emulator's
# benchMark-bracketed window is exactly ai.reset()+ai.think(). lineprof.js
# emits a "<byteAddrHex> <cycles>" histogram keyed by basic-block entry PC.
# THE ADDRESSES ARE BYTE ADDRESSES -- pass them to addr2line AS-IS (do NOT
# <<1 / *2). Under -flto, out-of-line engine fns (hasLiberty, simPlay,
# groupLibs*, regionVital, candidatePrior-inlined-in-widenNode, ...) keep
# correct DWARF line info; the ~2/3 of samples that land in LTO-fused
# partitions get mislabeled to other source files (USBCore/CDC/wiring) or
# unresolved -- so we FILTER to ai.cpp/game.cpp and rank within the engine.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"; SK="$HERE/../bench_avr"; OUT=/tmp/ardugo_line
CLI="/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"
BIN=~/Library/Arduino15/packages/arduino/tools/avr-gcc/7.3.0-atmel3.6.1-arduino7/bin
NM="$BIN/avr-nm"; A2L="$BIN/avr-addr2line"
ITERS=${1:-40}
"$CLI" compile --fqbn arduino:avr:leonardo \
  --build-property "compiler.cpp.extra_flags=-DBENCH_PAD=${BENCH_PAD:-12} -DMCTS_ITERATIONS=$ITERS $EXTRA" \
  --output-dir "$OUT" "$SK" >/dev/null
ELF="$OUT/bench_avr.ino.elf"
MARK=$("$NM" "$ELF" | grep -iw benchMark | awk '{print $1}'); MARK=${MARK: -4}
node "$HERE/lineprof.js" "$OUT/bench_avr.ino.hex" "$MARK" > "$OUT/pc.txt" 2>/dev/null
python3 - "$OUT/pc.txt" "$ELF" "$A2L" "$SK" <<'PY'
import sys,subprocess,collections,re
pc,elf,a2l,sk=sys.argv[1:5]; srcdir=sk.rstrip('/')+'/'
rows=[(x[0],int(x[1])) for x in (l.split() for l in open(pc)) if len(x)==2]
tot=sum(c for _,c in rows)
out=subprocess.run([a2l,"-ifCp","-e",elf,*[f"0x{a}" for a,_ in rows]],capture_output=True,text=True).stdout.split('\n')
recs=[];cur=[]
for ln in out:
    (cur.append(ln) if ln.startswith(' (inlined by') else (recs.append(cur) if cur else None, cur:=[ln])[-1])
if cur: recs.append(cur)
byline=collections.Counter(); fnof={}
for (a,c),rec in zip(rows,recs):
    m=re.search(r'^(.*?) at (\S+):(\d+)',rec[0])
    if not m: continue
    f=m.group(2).split('/')[-1]; l=int(m.group(3))
    if f in ('ai.cpp','game.cpp'): byline[(f,l)]+=c; fnof[(f,l)]=m.group(1).split('(')[0].split()[-1]
src={f:(open(srcdir+f).read().splitlines() if __import__('os').path.exists(srcdir+f) else []) for f in ('ai.cpp','game.cpp')}
print(f"engine (ai.cpp+game.cpp) = {100*sum(byline.values())/tot:.0f}% of window; per-line:\n")
for (f,l),c in byline.most_common(24):
    t=src[f][l-1].strip()[:58] if l-1<len(src[f]) else ""
    print(f"{100*c/tot:5.2f}%  {fnof[(f,l)]:14} {f}:{l:<4} {t}")
PY
