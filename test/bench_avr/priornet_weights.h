#pragma once
#include <avr/pgmspace.h>
// Learned prior net (pn_s0): 24 -> 8 -> 1, integer-exact export.
// pre_h = sum_i (f_i - PN_FMID[i]) * PN_W1[i*8+h]   (int32, scale S1=19.1390)
// out   = sum_h relu(pre_h + PN_B1[h]*? ) ... see check_prior.py reference.
// out is a monotone int32 score; caller scales into the bonus range.
#define PN_NF 24
#define PN_H 8
PROGMEM static const int8_t PN_W1[192] = {
    2, 1, -4, 1, -3, 4, 1, 4, -2, -5, -2, 0,
    -1, -4, 0, -5, -1, 0, -1, 1, -5, 6, -2, 4,
    5, 1, -4, -5, -3, 1, -6, -1, 0, 1, -1, 2,
    1, 0, 3, 2, 1, -2, 33, 2, -25, -20, 8, 10,
    6, 2, -1, -2, -7, 2, -3, 3, 19, 26, -2, -1,
    13, -13, 1, -1, 0, 1, 0, 1, 3, -2, 0, 0,
    -1, -4, 1, -1, 0, 0, 0, 2, 2, -3, 11, -11,
    -100, 33, 15, -23, 29, 37, 10, 7, -22, -20, 1, 12,
    17, 12, -20, -12, -31, 50, -11, -25, -30, -37, -3, 4,
    -2, 20, 0, -3, 21, -1, -18, -4, -49, 27, 1, 7,
    -1, 3, -5, -4, -4, 13, -19, 1, 6, -2, 1, -2,
    17, -9, 9, 2, 1, 2, -8, 3, -3, 8, 0, 0,
    -8, 1, -34, 5, -11, 5, 8, -10, 0, 0, 1, 0,
    0, 0, 0, 0, -4, -10, -1, 0, 7, 0, 0, -2,
    15, -14, -8, 0, 34, -18, 0, 7, -1, -4, -1, -3,
    -43, -4, 6, 1, -3, 3, -3, 8, 2, -3, -11, -8,
};
PROGMEM static const int16_t PN_B1[8] = {
    1, -10, 20, -1, 33, -4, 4, 4,
};
PROGMEM static const int8_t PN_V[8] = {
    59, 79, -59, 13, -100, -70, -21, 36,
};
PROGMEM static const int8_t PN_FMID[24] = {
    0, 0, 4, 0, 3, 2, 0, 0, 7, 7, 0, 0,
    0, 0, 0, 0, 0, 1, 0, 32, 0, 0, 0, 0,
};

// FMID folded at the pf[] write sites (2026-08-14): the kernel reads
// d = pf[i] directly; these constants MUST mirror PN_FMID[] above.
// The PRIOR_DUMP host path re-adds them before emission so the
// training-data contract (raw features) is unchanged.
#define PN_FM_2  4
#define PN_FM_4  3
#define PN_FM_5  2
#define PN_FM_8  7
#define PN_FM_9  7
#define PN_FM_17 1
#define PN_FM_19 32
