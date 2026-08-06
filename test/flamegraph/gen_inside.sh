#!/bin/bash
# Regenerate inside_$TAG.json (inlined-origin breakdown of think()/widenNode,
# with hot lines per part) from the lineprof2 trace. Run AFTER gen_profile.sh
# for the same TAG (it reads $OUT/lp2.txt that gen_profile.sh left behind).
# Env: SCR (output dir, default ./work), SK (bench sketch).
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
SCR="${SCR:-$DIR/work}"; mkdir -p "$SCR"
TAG="${TAG:-mid}"
OUT=/tmp/ardugo_prof_$TAG
SK="${SK:-$DIR/../bench_avr}"
A2L=~/Library/Arduino15/packages/arduino/tools/avr-gcc/7.3.0-atmel3.6.1-arduino7/bin/avr-addr2line
python3 - "$OUT/lp2.txt" "$OUT/bench_avr.ino.elf" "$A2L" "$SK" "$SCR" "$TAG" <<'PY'
import sys,subprocess,collections,json,re,os
lp,elf,a2l,sk,scr=sys.argv[1:6]; srcdir=sk.rstrip('/')+'/'
rows=[(int(p[0]),int(p[1]),int(p[2]),p[3].split('(')[0]) for p in
      (l.rstrip('\n').split(' ',3) for l in open(lp)) if len(p)==4]
tot=sum(c for _,c,_,_ in rows)
allpcs=sorted({pc for pc,_,_,_ in rows})
o=subprocess.run([a2l,"-fC","-e",elf,*[f"0x{pc*2:x}" for pc in allpcs]],capture_output=True,text=True).stdout.splitlines()
originfn={pc:o[2*i].split('(')[0].strip() for i,pc in enumerate(allpcs)}
loc={pc:o[2*i+1].strip().split('/')[-1] for i,pc in enumerate(allpcs)}
src={f:(open(srcdir+f).read().splitlines() if os.path.exists(srcdir+f) else []) for f in ('ai.cpp','game.cpp')}
def txt(l):
    m=re.match(r'(ai|game)\.cpp:(\d+)',l); n=int(m.group(2)) if m else 0
    return src[m.group(1)+'.cpp'][n-1].strip()[:58] if m and n and n-1<len(src[m.group(1)+'.cpp']) else ''
def build(leafset, selfname):
    # Group every PC whose symbol is in leafset by its addr2line ORIGIN
    # function (what got inlined in), with hot lines per origin.
    byfn=collections.Counter(); byfnline=collections.defaultdict(collections.Counter); lt=0
    byfnex=collections.defaultdict(collections.Counter)
    for pc,c,h,leaf in rows:
        if leaf in leafset:
            f=originfn[pc]; byfn[f]+=c; lt+=c; byfnline[f][loc[pc]]+=c
            byfnex[f][loc[pc]]+=h
    out=[]; acc=0
    for f,c in byfn.most_common():
        pm=100*c/tot
        if pm<0.30 and len(out)>=6: break
        lines=[]
        for l,lc in byfnline[f].most_common(7):
            if 100*lc/tot<0.08: break
            lines.append({'pct':round(100*lc/tot,2),'ex':byfnex[f][l],'loc':l,'text':txt(l)})
        out.append({'fn':f"{selfname} (own code)" if f==selfname else f,'pct':round(pm,2),'lines':lines})
        acc+=pm
    return {'total':round(100*lt/tot,1),'parts':out}
# think()'s own code + everything inlined into it lands under the bench's
# 'main' symbol; widenNode keeps its own symbol.
res={'think':build({'main'},'main'),'widenNode':build({'widenNode'},'widenNode')}
tag=sys.argv[6]
json.dump(res,open(scr+f'/inside_{tag}.json','w'),indent=0)
print(f"inside_{tag}: think {res['think']['total']}% ({len(res['think']['parts'])} parts), "
      f"widenNode {res['widenNode']['total']}% ({len(res['widenNode']['parts'])} parts)")
PY
echo "DONE"
