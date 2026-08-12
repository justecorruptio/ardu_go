#!/usr/bin/env python3
# NN retrain trainer: consumes C-dumped candidate features (nndump_probe) +
# KataGo policy labels. Two-CSR matmul design (nn_fast_mm lineage), segment
# softmax-CE, tail-average of last TAIL epochs + cosine anneal (the measured
# anti-variance recipe). Saves float tables as .npz.
#   nnret_train.py <prefix> <out.npz> <seed> [epochs=80] [H=8]
# Reads <prefix>_dump.txt (probe output), <prefix>_labels.jsonl, <prefix>_meta.jsonl
import sys, json, numpy as np
from scipy import sparse

PREFIX, OUT, SEED = sys.argv[1], sys.argv[2], int(sys.argv[3])
EPOCHS = int(sys.argv[4]) if len(sys.argv) > 4 else 80
H = int(sys.argv[5]) if len(sys.argv) > 5 else 8
BSZ_A = int(sys.argv[6]) if len(sys.argv) > 6 else 512
LR_A = float(sys.argv[7]) if len(sys.argv) > 7 else 0.10
TAIL, ANNEAL = 20, 15
NB, NW, NE = 15, 578, 98

labels = {json.loads(l)['id']: json.loads(l)['policy'] for l in open(PREFIX + "_labels.jsonl")}
weightof = {}
for l in open(PREFIX + "_meta.jsonl"):
    r = json.loads(l); weightof[r['id']] = r['weight']

# ---- parse dump ----
# per candidate: bcls, [(cls,woff)...], [fx...]; grouped per position
positions = []   # (id, [(cpos, bcls, stones, fxs)])
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

# ---- build CSR + targets ----
# vocab1 (linear): [B | W | Lf]  -> size NB + NW + NF
# vocab2 (hidden): [EB | E | F]  -> size NB + NE + NF   (B1 added as dense bias)
rows1, cols1, rows2, cols2 = [], [], [], []
tgt, seg, wrow = [], [], []
nrow = 0
kept = 0
for pid, cands in positions:
    pol = labels.get(pid)
    if not pol: continue
    t = np.zeros(len(cands))
    for i, (cpos, _, _, _) in enumerate(cands):
        t[i] = pol.get(str(cpos), 0.0)
    if t.sum() < 0.5: continue        # teacher mass off the candidate set
    t /= t.sum()
    w = float(weightof.get(pid, 1))
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
    tgt.append(t)
    wrow.append(w)
    nrow += len(cands)
    kept += 1
print(f"{kept} positions kept (teacher-mass filter), {nrow} candidate rows")

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
w1 = (rng.standard_normal(NB + NW + NF) * 0.01).astype(np.float64)
w2 = (rng.standard_normal((NB + NE + NF, H)) * 0.01).astype(np.float64)
b1 = np.zeros(H); v = rng.standard_normal(H) * 0.1

segs = np.array(seg)
seglen = segs[:, 1] - segs[:, 0]
T = np.concatenate(tgt)
W = np.array(wrow)
npos = len(segs)
LR0 = LR_A
BSZ = BSZ_A
MOM = 0.9
rng2 = np.random.default_rng(SEED + 1000)   # shuffle stream, separate from init
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
        Tb = T[rows]; Wb = W[pb]
        Wrow = np.repeat(Wb, sl)
        lin = Xb1 @ w1
        pre = (Xb2 @ w2) + b1
        act = np.maximum(pre, 0)
        sc = lin + act @ v
        starts = np.concatenate([[0], np.cumsum(sl)[:-1]])
        mx = np.maximum.reduceat(sc, starts)
        e = np.exp(sc - np.repeat(mx, sl))
        Z = np.add.reduceat(e, starts)
        P = e / np.repeat(Z, sl)
        epl += float(-np.sum(Wrow * Tb * np.log(P + 1e-12))); epw += Wb.sum()
        g = (P - Tb) * Wrow / Wb.sum()
        gv = act.T @ g
        gpre = np.outer(g, v) * (pre > 0)
        m1 = MOM * m1 + (Xb1.T @ g)
        m2 = MOM * m2 + (Xb2.T @ gpre)
        mb1 = MOM * mb1 + gpre.sum(0)
        mv = MOM * mv + gv
        w1 -= lr * m1; w2 -= lr * m2; b1 -= lr * mb1; v -= lr * mv
    if ep > EPOCHS - TAIL:
        cur = np.concatenate([w1, w2.ravel(), b1, v])
        avg = cur if avg is None else avg + cur
    if ep % 10 == 0 or ep == 1:
        print(f"epoch {ep}: CE {epl / epw:.4f} lr {lr:.3f}", flush=True)

avg /= TAIL
i = 0
w1a = avg[i:i + w1.size]; i += w1.size
w2a = avg[i:i + w2.size].reshape(w2.shape); i += w2.size
b1a = avg[i:i + H]; i += H
va = avg[i:i + H]
np.savez(OUT, w1=w1a, w2=w2a, b1=b1a, v=va,
         NB=NB, NW=NW, NE=NE, NF=NF, H=H, seed=SEED)
print(f"saved {OUT}")
