#!/usr/bin/env python3
# Export the learned prior net (24->H->1) to a C header, integer-exact.
# Folds the pow2 feature normalization into W1, quantizes to int8 with
# per-layer scales, and emits a python-side reference scorer so
# check_prior.py can assert C parity bit-for-bit.
#   export_prior_weights.py <net.npz> <out.h> [name]
import sys, numpy as np

z = np.load(sys.argv[1])
OUT = sys.argv[2]
NAME = sys.argv[3] if len(sys.argv) > 3 else "priornet"
W1, B1, V = z['W1'], z['B1'], z['V']
FMID, FDIV = z['FMID'], z['FDIV']
NF, H = int(z['NF']), int(z['H'])

# fold normalization: score = relu((x - FMID) @ (W1/FDIV) + B1) @ V
W1f = W1 / FDIV[:, None]

# layer-1: int16-accumulation mode (KERNEL16 env) scales W1 to +-15 so
# 24 products of int8 d x 5-bit w stay under int16; else int8 (+-100).
import os as _os
K16 = _os.environ.get("KERNEL16") == "1"
S1 = (15.0 if K16 else 100.0) / max(1e-9, np.abs(W1f).max())
W1q = np.clip(np.round(W1f * S1), -127, 127).astype(int)
B1q = np.round(B1 * S1 * 64).astype(int)          # B1 at S1*64: pre is
PRESHIFT = 6                                       # accumulated at S1, then
                                                   # <<6 headroom folded via
                                                   # B1 pre-scaled; relu in int32
# layer-2 int8 on relu(pre): out = sum_h relu_h * Vq[h] >> OUTSHIFT
S2 = 100.0 / max(1e-9, np.abs(V).max())
Vq = np.clip(np.round(V * S2), -127, 127).astype(int)

FMIDq = FMID.astype(int)

def emit_arr(f, typ, name, vals, per=12):
    f.write(f"PROGMEM static const {typ} {name}[{len(vals)}] = {{\n")
    for i in range(0, len(vals), per):
        f.write("    " + ", ".join(str(int(v)) for v in vals[i:i+per]) + ",\n")
    f.write("};\n")

with open(OUT, "w") as f:
    f.write(f"""#pragma once
#include <avr/pgmspace.h>
// Learned prior net ({NAME}): 24 -> {H} -> 1, integer-exact export.
// pre_h = sum_i (f_i - PN_FMID[i]) * PN_W1[i*{H}+h]   (int32, scale S1={S1:.4f})
// out   = sum_h relu(pre_h + PN_B1[h]*? ) ... see check_prior.py reference.
// out is a monotone int32 score; caller scales into the bonus range.
#define PN_NF {NF}
#define PN_H {H}
""")
    P4 = _os.environ.get("PACK4") == "1"
    if P4:
        # 4-bit signed nibbles, H-pairs: byte = W[i][h] | W[i][h+1]<<4
        w4 = np.clip(np.round(W1f * (7.0 / max(1e-9, np.abs(W1f).max()))), -8, 7).astype(int)
        flat = w4.reshape(-1)
        packed = [((int(flat[i]) + 16) % 16) | ((((int(flat[i+1]) + 16) % 16)) << 4)
                  for i in range(0, len(flat), 2)]
        f.write(f"#define PN_PACK4 1\n")
        emit_arr(f, "uint8_t", "PN_W1P", packed)
        globals()['W1q_eff'] = w4   # for the reference scorer
    else:
        emit_arr(f, "int8_t", "PN_W1", W1q.reshape(-1))
    emit_arr(f, "int16_t", "PN_B1", np.round(B1 * S1).astype(int))
    emit_arr(f, "int8_t", "PN_V", Vq)
    emit_arr(f, "int8_t", "PN_FMID", FMIDq)
    f.write("// FMID as compile-time constants: ai.cpp folds these at the pf[]\n"
            "// write sites (kernel reads d directly); every index must be\n"
            "// emitted so a net with different medians can't silently break\n"
            "// the fold.\n")
    for i, v in enumerate(FMIDq):
        f.write(f"#define PN_FM_{i} {int(v)}\n")

# integer reference + fidelity report
W1q_eff = None
def int_score(feats):
    global W1q_eff
    Wq = W1q_eff if W1q_eff is not None else W1q
    # scale B1 to the active W scale
    Sw = (7.0 / max(1e-9, np.abs(W1f).max())) if W1q_eff is not None else S1
    pre = [int(np.round(B1[h] * Sw)) for h in range(H)]
    for i in range(NF):
        d = int(feats[i]) - int(FMIDq[i])
        for h in range(H):
            pre[h] += d * int(Wq[i, h])
    out = 0
    for h in range(H):
        r = pre[h] if pre[h] > 0 else 0
        out += r * int(Vq[h])
    return out

def float_score(feats):
    x = (np.array(feats, dtype=np.float64) - FMID) / FDIV
    return float(np.maximum(x @ W1 + B1, 0) @ V)

rng = np.random.default_rng(3)
agree = 0
N = 2000
for _ in range(N):
    a = rng.integers(0, 5, NF); b = rng.integers(0, 5, NF)
    a[0] = rng.integers(-30, 30); b[0] = rng.integers(-30, 30)  # pattern range
    fa, fb = float_score(a), float_score(b)
    ia, ib = int_score(a), int_score(b)
    if abs(fa - fb) < 1e-9: agree += 1; continue
    agree += ((fa > fb) == (ia > ib))
print(f"exported {OUT}: {'K16 ' if K16 else ''}S1={S1:.4f} S2={S2:.4f} "
      f"pairwise order agreement float-vs-int: {100*agree/N:.2f}%")
np.savez(OUT + ".ref.npz", W1q=W1q, B1q=np.round(B1*S1).astype(int),
         Vq=Vq, FMIDq=FMIDq, S1=S1, S2=S2)
