#!/usr/bin/env python3
# Pick-parity: score the dumped candidates with (a) float tables, (b) the
# quantized nibble tables re-decoded from the exported header, and report
# argmax agreement. The C side is then cross-checked by rebuilding nndump
# with the new header and comparing ENDPOS best= lines.
#   check_argmax.py <prefix>_dump.txt <net.npz> <exported.h>
import sys, re, numpy as np

DUMP, NPZ, HDR = sys.argv[1], sys.argv[2], sys.argv[3]
npz = np.load(NPZ)
w1, w2, b1, v = npz['w1'], npz['w2'], npz['b1'], npz['v']
NB, NW, NE, NF, H = (int(npz[k]) for k in ('NB', 'NW', 'NE', 'NF', 'H'))

h = open(HDR).read()
KS = int(re.search(r"NN_KSCALE (\d+)", h).group(1))
def table(name):
    m = re.search(rf"NN_{name}\[\d+\] = \{{(.*?)\}};", h, re.S)
    bts = [int(x, 16) for x in re.findall(r"0x([0-9A-F]{2})", m.group(1))]
    out = []
    for b in bts:
        out.append(((b & 0xF) ^ 8) - 8)
        out.append(((b >> 4) ^ 8) - 8)
    return np.array(out)
Bq = table('B')[:NB]; Wq = table('W')[:NW]; Lfq = table('LF')[:NF]
Eq = table('E')[:NE*H].reshape(NE, H); EBq = table('EB')[:NB*H].reshape(NB, H)
Fq = table('F')[:NF*H].reshape(NF, H); B1q = table('B1')[:H]; Vq = table('V')[:H]

agree = tot = 0
cur = []
def score_pos(cands):
    fs, qs = [], []
    for bcls, stones, fxs in cands:
        lf = w1[NB], 0  # placeholder
        linf = w1[bcls] + sum(w1[NB + wo] for _, wo in stones) + sum(w1[NB + NW + fx] for fx in fxs)
        pref = w2[bcls] + b1 + sum(w2[NB + c] for c, _ in stones) + sum(w2[NB + NE + fx] for fx in fxs)
        fs.append(linf + np.maximum(pref, 0) @ v)
        linq = Bq[bcls] + sum(Wq[wo] for _, wo in stones) + sum(Lfq[fx] for fx in fxs)
        preq = EBq[bcls] + B1q + sum(Eq[c] for c, _ in stones) + sum(Fq[fx] for fx in fxs)
        qs.append(KS * linq + np.maximum(preq, 0) @ Vq)
    return int(np.argmax(fs)), int(np.argmax(qs))

for line in open(DUMP):
    p = line.split()
    if p[0] == "CAND":
        ei = p.index("E"); fi = p.index("F")
        cur.append((int(p[3]),
                    [tuple(map(int, s.split(":"))) for s in p[ei+1:fi]],
                    [int(x) for x in p[fi+1:]]))
    elif p[0] == "ENDPOS" and cur:
        a, b = score_pos(cur)
        agree += (a == b); tot += 1
        cur = []
print(f"float-vs-quantized argmax agreement: {agree}/{tot} = {100*agree/max(tot,1):.1f}%")
