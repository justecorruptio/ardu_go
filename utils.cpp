#include "utils.h"

// Decimal, unsigned. The 16-bit version exists for the AI stats
// panel (visit counts, think ms); itoa forwards to it so there is
// only one division loop in flash.
// Caller-provided buffer (was a 6-byte static; prNum is the only
// device caller and stacks it now)
char * itoa16(uint16_t x, char *itoa_buf) {
    char *p = itoa_buf + 5;
    *p = 0;
    do {
        *--p = '0' + x % 10;
        x /= 10;
    } while(x);

    // Must immediately use this.
    return p;
}

char * itoa(uint8_t x, char *buf) {
    return itoa16(x, buf);
}

uint8_t strlen(const uint8_t* s) {
    const uint8_t* t = s;
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
