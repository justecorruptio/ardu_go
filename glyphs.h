#pragma once
#include <avr/pgmspace.h>

// 7x7 filled circle (black stone)
// ..###..
// .#####.
// #######
// #######
// #######
// .#####.
// ..###..
PROGMEM const uint8_t GLYPH_STONE_FILLED[] = {
    0x1C, 0x3E, 0x7F, 0x7F, 0x7F, 0x3E, 0x1C
};

// 7x7 circle outline (white stone)
// ..###..
// .#...#.
// #.....#
// #.....#
// #.....#
// .#...#.
// ..###..
PROGMEM const uint8_t GLYPH_STONE_OUTLINE[] = {
    0x1C, 0x22, 0x41, 0x41, 0x41, 0x22, 0x1C
};
