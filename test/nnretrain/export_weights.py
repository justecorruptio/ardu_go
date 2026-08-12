#!/usr/bin/env python3
# NN retrain: export trained float tables (.npz) to nn_open_weights.h format
# (4-bit signed nibbles, low-first). Scale tying: score_c = KS*lin_q + v_q.relu(pre_q)
# is proportional to the float score iff gamma = KS*alpha*beta, where
#   alpha = pre-table step (EB/E/F/B1), beta = V step, gamma = lin step (B/W/Lf).
# KS = ceil(max|lin|/(7*alpha*beta)) keeps the lin path in nibble range.
#   export_weights.py <in.npz> <out.h> [name]
import sys, numpy as np

npz = np.load(sys.argv[1])
OUT = sys.argv[2]
NAME = sys.argv[3] if len(sys.argv) > 3 else "nnret"
w1, w2, b1, v = npz['w1'], npz['w2'], npz['b1'], npz['v']
NB, NW, NE, NF, H = int(npz['NB']), int(npz['NW']), int(npz['NE']), int(npz['NF']), int(npz['H'])

alpha = max(np.abs(w2).max(), np.abs(b1).max()) / 7.0
beta = np.abs(v).max() / 7.0
KS = max(1, int(np.ceil(np.abs(w1).max() / (7.0 * alpha * beta))))
gamma = KS * alpha * beta

def q(x, step):
    return np.clip(np.round(x / step), -8, 7).astype(int)

B_q = q(w1[:NB], gamma)
W_q = q(w1[NB:NB + NW], gamma)
Lf_q = q(w1[NB + NW:], gamma)
EB_q = q(w2[:NB], alpha)          # [NB][H]
E_q = q(w2[NB:NB + NE], alpha)    # [NE][H]
F_q = q(w2[NB + NE:], alpha)      # [NF][H]
B1_q = q(b1, alpha)               # [H]
V_q = q(v, beta)                  # [H]

def pack(vals):
    vals = list(vals)
    if len(vals) % 2: vals.append(0)
    out = []
    for i in range(0, len(vals), 2):
        lo = (vals[i] + 16) % 16
        hi = (vals[i + 1] + 16) % 16
        out.append(lo | (hi << 4))
    return out

def emit(f, name, vals):
    b = pack(vals)
    f.write(f"PROGMEM const uint8_t {name}[{len(b)}] = {{ // {len(vals)} nibbles\n")
    for i in range(0, len(b), 20):
        f.write("    " + ", ".join(f"0x{x:02X}" for x in b[i:i+20]) + ",\n")
    f.write("};\n")

with open(OUT, "w") as f:
    f.write(f"""#pragma once
#include <avr/pgmspace.h>
// Opening-net weights ({NAME}): NN-retrain pipeline (test/nnretrain/), 4-bit,
// human-corpus positions, b18-9x9 policy teacher. Formula unchanged:
// score = KSCALE*(NN_B[bcls] + sum NN_W + sum NN_LF) + sum_h NN_V[h]*relu(pre_h)
#define NN_H {H}
#define NN_BITS 4
#define NN_KSCALE {KS}
#ifndef NN_MAX_STONES
#define NN_MAX_STONES 24
#endif
""")
    emit(f, "NN_B", B_q)
    emit(f, "NN_W", W_q)
    emit(f, "NN_E", E_q.ravel())
    emit(f, "NN_EB", EB_q.ravel())
    emit(f, "NN_B1", B1_q)
    emit(f, "NN_V", V_q)
    emit(f, "NN_F", F_q.ravel())
    emit(f, "NN_LF", Lf_q)

# quantization fidelity report: float vs quantized argmax over random synthetic
# candidates is meaningless — real check runs on the dump (see check_argmax.py).
print(f"exported {OUT}: H={H} KS={KS} alpha={alpha:.4g} beta={beta:.4g} gamma={gamma:.4g}")
print(f"nibble sat: B {np.mean(np.abs(B_q)==8):.2f} W {np.mean(np.abs(W_q)==8):.2f} "
      f"E {np.mean(np.abs(E_q)==8):.2f} F {np.mean(np.abs(F_q)==8):.2f}")
