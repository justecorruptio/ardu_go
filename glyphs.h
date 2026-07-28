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

// Selection cursor: four corner Ls of the 7x7 cell box. Columns are
// (bit0 = top row): 0x63 = rows {0,1,5,6}, 0x41 = rows {0,6}.
PROGMEM const uint8_t GLYPH_CURSOR[] = {
    0x63, 0x41, 0x00, 0x00, 0x00, 0x41, 0x63
};

// Last-move "v" marker, drawn at (x-1, y): cols (bit0=top) are
// row0, row1, row0 -> pixels (x-1,y) (x,y+1) (x+1,y).
PROGMEM const uint8_t GLYPH_LASTMOVE[] = {
    0x01, 0x02, 0x01
};
