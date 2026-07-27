#include "utils.h"

// Decimal, unsigned 0-255 only: every live caller prints small board
// counts, and the old signed any-base version cost ~100 bytes of
// flash for commented-out debug code.
char itoa_buf[4];
char * itoa(uint8_t x) {
    char *p = itoa_buf + 3;
    *p = 0;
    do {
        *--p = '0' + x % 10;
        x /= 10;
    } while(x);

    // Must immediately use this.
    return p;
}

uint8_t strlen(const uint8_t* s) {
    uint8_t* t = s;
    while(*t) t++;
    return t - s;
}

uint8_t popcount(uint8_t n) {
    n = ((n & 0xaa) >> 1) + ((n & 0x55) >> 0);
    n = ((n & 0xcc) >> 2) + ((n & 0x33) >> 0);
    n = ((n & 0xf0) >> 4) + ((n & 0x0f) >> 0);
    return n;
};

/*
uint8_t log2(uint16_t v) {
    return pgm_read_byte(GRID_DEBRUIJN_16 + ((-v & v) % 19));
}
*/
uint8_t log2(uint16_t v) {
    uint8_t c = 16;
    v &= -signed(v);
    if (v) c--;
    if (v & 0x00FF) c -= 8;
    if (v & 0x0F0F) c -= 4;
    if (v & 0x3333) c -= 2;
    if (v & 0x5555) c -= 1;
    return c;
}
