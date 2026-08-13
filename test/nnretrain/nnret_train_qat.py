#!/usr/bin/env python3
# QAT variant of nnret_train_cost.py: the forward pass scores with weights
# snapped to the EXACT export grid (export_weights.py: 4-bit nibbles,
# alpha/beta/KS/gamma scale tying, clip [-8,7]) and gradients flow to the
# float masters via clipped STE (saturated weights get zero gradient).
# Optimizes the deployed quantity: QUANTIZED expected winrate cost. cost(c) = best_wr -
# wr(c) from 48-visit moveInfos; candidates the search never visited get the
# worst observed cost (bounded pessimism). Consequentiality weighting is
# intrinsic: grad ~ p*(cost - E[cost]) vanishes where the pick doesn't matter.
#   nnret_train_cost.py <prefix> <out.npz> <seed> [epochs] [H] [BSZ] [LR]
# Reads <prefix>_dump.txt, <prefix>_costs.jsonl, <prefix>_meta.jsonl
import sys, json, numpy as np
from scipy import sparse

PREFIX, OUT, SEED = sys.argv[1], sys.argv[2], int(sys.argv[3])
EPOCHS = int(sys.argv[4]) if len(sys.argv) > 4 else 120
H = int(sys.argv[5]) if len(sys.argv) > 5 else 8
BSZ = int(sys.argv[6]) if len(sys.argv) > 6 else 512
LR0 = float(sys.argv[7]) if len(sys.argv) > 7 else 0.10
TAIL, ANNEAL = 20, 15
NB, NW, NE = 15, 578, 98

costs = {}
for l in open(PREFIX + "_costs.jsonl"):
    r = json.loads(l); costs[r['id']] = r
weightof = {}
turnof = {}
for l in open(PREFIX + "_meta.jsonl"):
    r = json.loads(l); weightof[r['id']] = r['weight']; turnof[r['id']] = r['turn']

positions = []
cur = None
maxfx = 0
for line in open(PREFIX + "_dump.txt"):
    p = line.split()
    if p[0] == "POS":
        cur = (p[1], [])
    elif p[0] == "CAND":
        cpos = int(p[1]); bcls = int(p[3])
        ei = p.index("E"); fi = p.index("F")
        stones = [tuple(map(int, s.split(":"))) for s in p[ei+1:fi]]
        fxs = [int(x) for x in p[fi+1:]]
        if fxs: maxfx = max(maxfx, max(fxs))
        cur[1].append((cpos, bcls, stones, fxs))
    elif p[0] == "ENDPOS":
        if cur and cur[1]: positions.append(cur)
        cur = None
NF = maxfx + 1
print(f"{len(positions)} positions parsed, NF={NF}")

cols1, cols2 = [], []
cvec, seg, wrow = [], [], []
nrow = 0
kept = 0
for pid, cands in positions:
    lab = costs.get(pid)
    if not lab or not lab.get('wr'): continue
    # labels are BLACK-perspective (4090 cfg: reportAnalysisWinratesAs=BLACK);
    # convert to MOVER perspective -- the audit bug that produced the 171/179
    # inverted-cost nets (White-to-move positions had best=worst).
    flip = (turnof[pid] == 'W')
    wr = {int(k): (1.0 - v if flip else v) for k, v in lab['wr'].items()}
    best = max(wr.values())
    CLIP = float(__import__('os').environ.get('COST_CLIP', '0')) or None
    cc = np.empty(len(cands))
    for i, (cpos, _, _, _) in enumerate(cands):
        if CLIP:
            # v2: clip disasters (25%-loss and 50%-loss are both "never play";
            # spend resolution near the top) and put unvisited at the ceiling
            # instead of worst-fill (which taught search-coverage imitation)
            cc[i] = min(best - wr[cpos], CLIP) if cpos in wr else CLIP
        else:
            worst_cost = best - min(wr.values())
            cc[i] = best - wr[cpos] if cpos in wr else max(worst_cost, 0.10)
    for i, (cpos, bcls, stones, fxs) in enumerate(cands):
        r = nrow + i
        cols1.append((r, bcls))
        for cls, woff in stones:
            cols1.append((r, NB + woff))
            cols2.append((r, NB + cls))
        for fx in fxs:
            cols1.append((r, NB + NW + fx))
            cols2.append((r, NB + NE + fx))
        cols2.append((r, bcls))
    seg.append((nrow, nrow + len(cands)))
    cvec.append(cc)
    wrow.append(float(weightof.get(pid, 1)))
    nrow += len(cands)
    kept += 1
print(f"{kept} positions kept, {nrow} candidate rows")

def csr(entries, ncol):
    r = np.array([e[0] for e in entries]); c = np.array([e[1] for e in entries])
    d = np.ones(len(entries), dtype=np.float32)
    m = sparse.coo_matrix((d, (r, c)), shape=(nrow, ncol)).tocsr()
    m.sum_duplicates()
    return m
X1 = csr(cols1, NB + NW + NF)
X2 = csr(cols2, NB + NE + NF)
print(f"CSR: X1 nnz {X1.nnz}, X2 nnz {X2.nnz}")

rng = np.random.default_rng(SEED)
WARM = __import__('os').environ.get('WARMSTART')
if WARM:
    wz = np.load(WARM)
    w1 = wz['w1'].copy(); w2 = wz['w2'].copy()
    b1 = wz['b1'].copy(); v = wz['v'].copy()
    print(f"warm-started from {WARM}")
else:
    w1 = (rng.standard_normal(NB + NW + NF) * 0.01).astype(np.float64)
    w2 = (rng.standard_normal((NB + NE + NF, H)) * 0.01).astype(np.float64)
    b1 = np.zeros(H); v = rng.standard_normal(H) * 0.1

segs = np.array(seg)
seglen = segs[:, 1] - segs[:, 0]
C = np.concatenate(cvec)
W = np.array(wrow)
npos = len(segs)
MOM = 0.9
rng2 = np.random.default_rng(SEED + 1000)
m1 = np.zeros_like(w1); m2 = np.zeros_like(w2)
mb1 = np.zeros_like(b1); mv = np.zeros_like(v)
avg = None
for ep in range(1, EPOCHS + 1):
    lr = LR0
    if ep > EPOCHS - ANNEAL:
        k = (ep - (EPOCHS - ANNEAL)) / ANNEAL
        lr = LR0 * 0.5 * (1 + np.cos(np.pi * k))
    perm = rng2.permutation(npos)
    epl = 0.0; epw = 0.0
    for bi in range(0, npos, BSZ):
        pb = perm[bi:bi + BSZ]
        rows = np.concatenate([np.arange(segs[i, 0], segs[i, 1]) for i in pb])
        sl = seglen[pb]
        Xb1 = X1[rows]; Xb2 = X2[rows]
        Cb = C[rows]; Wb = W[pb]
        Wrow = np.repeat(Wb, sl)
        # ---- QAT: snap masters to the export grid (dequantized) ----
        alpha = max(np.abs(w2).max(), np.abs(b1).max()) / 7.0
        beta = np.abs(v).max() / 7.0
        KS = max(1, int(np.ceil(np.abs(w1).max() / (7.0 * alpha * beta))))
        gamma = KS * alpha * beta
        def snap(w, step):
            r = np.round(w / step)
            return np.clip(r, -8, 7) * step, (r >= -8) & (r <= 7)
        w1q, k1 = snap(w1, gamma)
        w2q, k2 = snap(w2, alpha)
        b1q, kb = snap(b1, alpha)
        vq, kv = snap(v, beta)
        lin = Xb1 @ w1q
        pre = (Xb2 @ w2q) + b1q
        act = np.maximum(pre, 0)
        sc = lin + act @ vq
        starts = np.concatenate([[0], np.cumsum(sl)[:-1]])
        mx = np.maximum.reduceat(sc, starts)
        e = np.exp(sc - np.repeat(mx, sl))
        Z = np.add.reduceat(e, starts)
        P = e / np.repeat(Z, sl)
        Ecost = np.add.reduceat(P * Cb, starts)         # expected cost per position
        epl += float(np.dot(Wb, Ecost)); epw += Wb.sum()
        g = P * (Cb - np.repeat(Ecost, sl)) * Wrow / Wb.sum()
        gv = act.T @ g
        gpre = np.outer(g, vq) * (pre > 0)
        m1 = MOM * m1 + (Xb1.T @ g) * k1
        m2 = MOM * m2 + (Xb2.T @ gpre) * k2
        mb1 = MOM * mb1 + gpre.sum(0) * kb
        mv = MOM * mv + gv * kv
        w1 -= lr * m1; w2 -= lr * m2; b1 -= lr * mb1; v -= lr * mv
    if ep > EPOCHS - TAIL:
        curw = np.concatenate([w1, w2.ravel(), b1, v])
        avg = curw if avg is None else avg + curw
    if ep % 10 == 0 or ep == 1:
        print(f"epoch {ep}: E[cost] {100*epl/epw:.3f}% lr {lr:.3f}", flush=True)

avg /= TAIL
i = 0
w1a = avg[i:i + w1.size]; i += w1.size
w2a = avg[i:i + w2.size].reshape(w2.shape); i += w2.size
b1a = avg[i:i + H]; i += H
va = avg[i:i + H]
np.savez(OUT, w1=w1a, w2=w2a, b1=b1a, v=va,
         NB=NB, NW=NW, NE=NE, NF=NF, H=H, seed=SEED)
print(f"saved {OUT}")
