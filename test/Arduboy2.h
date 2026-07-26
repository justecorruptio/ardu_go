// Host-side stub of the Arduboy2 library: just enough for ai.cpp and
// game.cpp to compile natively for strength testing.
#pragma once
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define PROGMEM
#define pgm_read_byte(a) (*(const uint8_t *)(a))
#define pgm_read_word(a) (*(const uint16_t *)(a))

class Arduboy2Base {
public:
    static uint8_t sBuffer[1024];
};

inline long random(long m) { return m > 0 ? rand() % m : 0; }
