#pragma once

#include <stdint.h>
#include <avr/pgmspace.h>

PROGMEM const int8_t GRID_DEBRUIJN_16 [] = {
    -1, 0, 1, 13, 2, -1, 14, 6, 3, 8, -1, 12, 15, 5, 7, 11, 4, 10, 9
};

char * itoa(int x, int base=10);
uint8_t strlen(const uint8_t* s);
uint8_t popcount(uint8_t n);
uint8_t log2(uint16_t v);

//char * loadPStr(char * pAddr);
