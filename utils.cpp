#include "utils.h"

char itoa_buf[9] = "\0\0\0\0\0\0\0\0";
char * itoa(int x, int base) {
    uint8_t i = 7;
    int neg = 0;
    if(x < 0){
        x = -x;
        neg = 1;
    }
    do {
        char c = (x % base) + '0';
        if (c > '9'){
            c = c - '9' + 'A' - 1;
        }
        itoa_buf[i--] = c;
        x /= base;
    } while(x);
    if(neg){
        itoa_buf[i--] = '-';
    }

    // Must immediately use this.
    return itoa_buf + i + 1;
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
