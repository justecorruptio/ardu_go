#!/bin/bash
# Regenerate profile_folded_$TAG.folded (avrprof call-stack profiler) +
# profile_calls_$TAG.txt + perline_$TAG.json for the current working tree.
# Same default build as ../emu/run.sh so proportions match the run.sh absolute.
#   TAG=mid ./gen_profile.sh            # 26-stone midgame, 400 iters
#   EXTRA=-DBENCH_OPENING TAG=open ./gen_profile.sh   # 8-stone opening
# Env: SCR (output dir, default ./work), SK (bench sketch), HERE (emu dir).
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
SCR="${SCR:-$DIR/work}"; mkdir -p "$SCR"
HERE="${HERE:-$DIR/../emu}"
SK="${SK:-$DIR/../bench_avr}"
TAG="${TAG:-mid}"
EXTRA="${EXTRA:-}"
OUT=/tmp/ardugo_prof_$TAG
CLI="/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"
BIN=~/Library/Arduino15/packages/arduino/tools/avr-gcc/7.3.0-atmel3.6.1-arduino7/bin
NM="$BIN/avr-nm"; A2L="$BIN/avr-addr2line"
echo "[1/4] compile bench_avr @ 400 iters (default flags)"
"$CLI" compile --clean --fqbn arduino:avr:leonardo \
  --build-property "compiler.cpp.extra_flags=-DMCTS_ITERATIONS=400 $EXTRA" \
  --output-dir "$OUT" "$SK" >/dev/null
ELF="$OUT/bench_avr.ino.elf"; HEX="$OUT/bench_avr.ino.hex"
"$NM" -CnS --defined-only "$ELF" > "$OUT/sym.nm"
MARK=$("$NM" "$ELF" | grep -iw benchMark | awk '{print $1}'); MARK=${MARK: -4}
echo "[2/4] avrprof -> profile_folded.folded (+ profile_calls.txt)"
node "$HERE/avrprof.js"  "$HEX" "$OUT/sym.nm" "$MARK" "$SCR/profile_calls_$TAG.txt" > "$SCR/profile_folded_$TAG.folded" 2>/dev/null
echo "[3/4] lineprof2 -> lp2.txt"
node "$HERE/lineprof2.js" "$HEX" "$OUT/sym.nm" "$MARK" > "$OUT/lp2.txt" 2>/dev/null
echo "[4/4] build perline.json"
python3 - "$OUT/lp2.txt" "$ELF" "$A2L" "$SK" "$SCR" "$TAG" <<'PY'
import sys,subprocess,collections,re,json,os
lp,elf,a2l,sk,scr=sys.argv[1:6]; srcdir=sk.rstrip('/')+'/'
rows=[(int(p[0]),int(p[1]),int(p[2]),p[3].split('(')[0]) for p in
      (l.rstrip('\n').split(' ',3) for l in open(lp)) if len(p)==4]
tot=sum(c for _,c,_,_ in rows)
pcs=sorted({pc for pc,_,_,_ in rows})
o=subprocess.run([a2l,"-fC","-e",elf,*[f"0x{pc*2:x}" for pc in pcs]],
                 capture_output=True,text=True).stdout.splitlines()
loc={pc:(o[2*i+1].strip().split('/')[-1] if 2*i+1<len(o) else '?') for i,pc in enumerate(pcs)}
byleaf=collections.Counter(); byline=collections.defaultdict(collections.Counter)
byex=collections.defaultdict(collections.Counter)
for pc,c,h,leaf in rows:
    byleaf[leaf]+=c; byline[leaf][loc[pc]]+=c; byex[leaf][loc[pc]]+=h
src={f:(open(srcdir+f).read().splitlines() if os.path.exists(srcdir+f) else []) for f in ('ai.cpp','game.cpp')}
def txt(l):
    m=re.match(r'(ai|game)\.cpp:(\d+)',l); n=int(m.group(2)) if m else 0
    return src[m.group(1)+'.cpp'][n-1].strip()[:56] if m and n and n-1<len(src[m.group(1)+'.cpp']) else ''
out=[]
for leaf,cnt in byleaf.most_common(12):
    if leaf in ('setup','???') or leaf.startswith('__') or 100*cnt/tot < 4.0: continue  # skip libgcc helpers
    lines=[]
    for l,c in byline[leaf].most_common(10):
        if 100*c/tot < 0.18: break
        lines.append({'pct':round(100*c/tot,2),'ex':byex[leaf][l],'loc':l,'text':txt(l)})
    out.append({'fn':leaf,'pct':round(100*cnt/tot,1),'lines':lines})
tag=sys.argv[6]
json.dump(out,open(scr+f'/perline_{tag}.json','w'),indent=0)
print(f"total think {tot} cyc; wrote perline.json with {len(out)} functions")
for f in out: print(f"  {f['pct']:5.1f}%  {f['fn']}")
PY
echo "DONE"
