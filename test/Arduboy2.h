// Host-side stub of the Arduboy2 library: just enough for ai.cpp and
// game.cpp to compile natively for strength testing.
#pragma once
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define PROGMEM
#define pgm_read_byte(a) (*(const uint8_t *)(a))
#define pgm_read_word(a) (*(const uint16_t *)(a))
#define pgm_read_dword(a) (*(const uint32_t *)(a))
#define pgm_read_ptr(a) (*(void * const *)(a))

#ifndef WIDTH
#define WIDTH 128
#define HEIGHT 64
#endif
class __FlashStringHelper;
#ifndef F
#define F(s) ((const __FlashStringHelper *)(s))
#endif

class Arduboy2Base {
public:
    static uint8_t sBuffer[1024];
    // Host mirror of Arduboy2's drawPixel (paged sBuffer layout) so
    // jaylib/display compile natively for the screen-diff probe
    // (test/scrprobe.cpp). Additive: engine host builds ignore it.
    static void drawPixel(int16_t x, int16_t y, uint8_t color) {
        if(x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) return;
        uint16_t i = (uint16_t)(y / 8) * WIDTH + x;
        uint8_t b = (uint8_t)(1 << (y & 7));
        if(color) sBuffer[i] |= b; else sBuffer[i] &= (uint8_t)~b;
    }
    static void display() {}
};

inline long random(long m) { return m > 0 ? rand() % m : 0; }
#ifndef PGM_P
#define PGM_P const char*
#endif
#ifndef strlen_P
#define strlen_P strlen
#endif
inline unsigned long millis() { return 256; }  // deterministic for scrprobe
