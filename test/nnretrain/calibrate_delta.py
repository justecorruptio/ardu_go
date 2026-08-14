#!/usr/bin/env python3
# Delta-sampling calibration: price opening variety in E[cost].
# Policy: uniform over candidates within DELTA of the quantized argmax
# (device int scores, exact export nibbles). Sweeps DELTA and reports,
# per stone-count band, the E[cost] increase vs argmax and the mean window
# size (variety). Mover-perspective cost from the 48-visit labels.
#   calibrate_delta.py <prefix> <exported.h> [max_stones]
import sys, json, re, numpy as np

PREFIX, HDR = sys.argv[1], sys.argv[2]
MAXST = int(sys.argv[3]) if len(sys.argv) > 3 else 10
NB, NW, NE = 15, 578, 98

h = open(HDR).read()
KS = int(re.search(r"NN_KSCALE (\d+)", h).group(1))
H = int(re.search(r"NN_H (\d+)", h).group(1))
def table(name):
    m = re.search(rf"NN_{name}\[\d+\] = \{{(.*?)\}};", h, re.S)
    bts = [int(x, 16) for x in re.findall(r"0x([0-9A-F]{2})", m.group(1))]
    out = []
    for b in bts:
        out.append(((b & 0xF) ^ 8) - 8)
        out.append(((b >> 4) ^ 8) - 8)
    return np.array(out)
NF = 74
Bq = table('B')[:NB]; Wq = table('W')[:NW]; Lfq = table('LF')[:NF]
Eq = table('E')[:NE*H].reshape(NE, H); EBq = table('EB')[:NB*H].reshape(NB, H)
Fq = table('F')[:NF*H].reshape(NF, H); B1q = table('B1')[:H]; Vq = table('V')[:H]

costs = {}
for l in open(PREFIX + "_costs.jsonl"):
    r = json.loads(l); costs[r['id']] = r
turnof = {}
for l in open(PREFIX + "_meta.jsonl"):
    r = json.loads(l); turnof[r['id']] = r['turn']

# positions: [(pid, stones_on_board, [(cpos, score_int)], {cpos: cost})]
POS = []
cur, pid = [], None
for line in open(PREFIX + "_dump.txt"):
    p = line.split()
    if p[0] == "POS":
        pid = p[1]; cur = []
    elif p[0] == "CAND":
        ei = p.index("E"); fi = p.index("F")
        stones = [tuple(map(int, s.split(":"))) for s in p[ei+1:fi]]
        fxs = [int(x) for x in p[fi+1:]]
        cur.append((int(p[1]), int(p[3]), stones, fxs))
    elif p[0] == "ENDPOS" and cur and pid is not None:
        nst = len(cur[0][2])
        if nst <= MAXST:
            lab = costs.get(pid)
            if lab and lab.get('wr'):
                flip = (turnof.get(pid) == 'W')
                wr = {int(k): (1.0 - v if flip else v) for k, v in lab['wr'].items()}
                best = max(wr.values())
                worst = best - min(wr.values())
                sc = []
                for cpos, bcls, stones, fxs in cur:
                    linq = Bq[bcls] + sum(Wq[wo] for _, wo in stones) + sum(Lfq[fx] for fx in fxs)
                    preq = EBq[bcls] + B1q + sum(Eq[c] for c, _ in stones) + sum(Fq[fx] for fx in fxs)
                    sc.append((cpos, int(KS * linq + np.maximum(preq, 0) @ Vq)))
                cc = {cpos: (best - wr[cpos] if cpos in wr else max(worst, 0.10))
                      for cpos, _ in sc}
                POS.append((nst, sc, cc))
        cur = []
print(f"{len(POS)} positions <= {MAXST} stones", file=sys.stderr)

BANDS = [(1, 4), (5, 8), (9, MAXST)]
# DELTA in units of the int score; sweep as fractions of the score spread
spread = np.median([max(s for _, s in sc) - min(s for _, s in sc) for _, sc, _ in POS])
print(f"median score spread: {spread:.0f}")
for lo, hi in BANDS:
    sel = [(sc, cc) for nst, sc, cc in POS if lo <= nst <= hi]
    if not sel: continue
    base = np.mean([cc[max(sc, key=lambda t: t[1])[0]] for sc, cc in sel])
    row = [f"stones {lo}-{hi} (n={len(sel)}): argmax {100*base:.2f}%"]
    for frac in (0.01, 0.02, 0.04, 0.08):
        D = spread * frac
        tot, win = 0.0, 0.0
        for sc, cc in sel:
            mx = max(s for _, s in sc)
            w = [cpos for cpos, s in sc if s > mx - D]
            tot += sum(cc[c] for c in w) / len(w)
            win += len(w)
        row.append(f"D={frac:.2f}sp: {100*tot/len(sel):.2f}% w={win/len(sel):.1f}")
    print("  ".join(row))
