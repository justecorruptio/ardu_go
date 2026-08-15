#pragma once
#include <avr/pgmspace.h>
// Learned prior net (priornet_h4): 24 -> 4 -> 1, integer-exact export.
// pre_h = sum_i (f_i - PN_FMID[i]) * PN_W1[i*4+h]   (int32, scale S1=19.6446)
// out   = sum_h relu(pre_h + PN_B1[h]*? ) ... see check_prior.py reference.
// out is a monotone int32 score; caller scales into the bonus range.
#define PN_NF 24
#define PN_H 4
PROGMEM static const int8_t PN_W1[96] = {
    0, -1, -3, -9, -2, 5, 9, 10, 1, 5, -7, -7,
    -14, -1, -7, -4, -1, 0, -3, -1, 43, 29, 0, 13,
    11, 5, -1, -10, 25, -30, 0, -2, -1, -2, 1, 0,
    0, 1, 0, 1, 57, 100, 50, -81, 70, 35, 42, 14,
    12, 36, 18, -47, -40, -3, -9, 30, 22, 26, 6, -84,
    8, 25, 17, -7, -18, -30, 20, 4, -9, -8, 1, -14,
    -8, -2, -6, -20, 0, 0, 0, 0, -16, -7, -1, 0,
    -19, -42, -11, 12, 42, 47, -29, -14, -3, -4, 1, 5,
};
PROGMEM static const int16_t PN_B1[4] = {
    3, -17, -23, 50,
};
PROGMEM static const int8_t PN_V[4] = {
    95, -100, 50, -91,
};
PROGMEM static const int8_t PN_FMID[24] = {
    0, 0, 4, 0, 3, 2, 0, 0, 7, 7, 0, 0,
    0, 0, 0, 0, 0, 1, 0, 32, 0, 0, 0, 0,
};
// FMID as compile-time constants: ai.cpp folds these at the pf[]
// write sites (kernel reads d directly); every index must be
// emitted so a net with different medians can't silently break
// the fold.
#define PN_FM_0 0
#define PN_FM_1 0
#define PN_FM_2 4
#define PN_FM_3 0
#define PN_FM_4 3
#define PN_FM_5 2
#define PN_FM_6 0
#define PN_FM_7 0
#define PN_FM_8 7
#define PN_FM_9 7
#define PN_FM_10 0
#define PN_FM_11 0
#define PN_FM_12 0
#define PN_FM_13 0
#define PN_FM_14 0
#define PN_FM_15 0
#define PN_FM_16 0
#define PN_FM_17 1
#define PN_FM_18 0
#define PN_FM_19 32
#define PN_FM_20 0
#define PN_FM_21 0
#define PN_FM_22 0
#define PN_FM_23 0
