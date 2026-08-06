#pragma once

#include <Arduboy2.h>
#include "constants.h"

// Shared flood-fill work stack (defined in game.cpp). Also used by the
// AI's playout engine — safe because game logic and search never run a
// flood at the same time, and no state persists between calls.
extern uint8_t floodScratch[BOARD_CELLS];

// Plain stack indexing — no 0x800 redirect. test/checkmagic.sh asserts
// at build time that floodScratch clears RAM 0x800-0x801, so a stomped
// entry can never feed a bogus board position back into a flood.
inline uint8_t& floodSlot(uint8_t i) {
    return floodScratch[i];
}

// Boards on the game side are packed 2 bits per cell (4 cells/byte):
// they are touched only on real moves and rendering, so the shift cost
// is invisible — unlike the AI's simBoard, which stays byte-per-cell
// for playout speed.
#define PACKED_BOARD_BYTES ((BOARD_CELLS + 3) / 4)

inline uint8_t packedGet(const uint8_t *b, uint8_t i) {
    return (b[i >> 2] >> ((i & 3) << 1)) & 3;
}

inline void packedSet(uint8_t *b, uint8_t i, uint8_t v) {
    uint8_t s = (i & 3) << 1;
    b[i >> 2] = (b[i >> 2] & ~(3 << s)) | (v << s);
}

class Game {
    public:
    uint8_t board[PACKED_BOARD_BYTES];   // EMPTY, BLACK, or WHITE
    uint8_t prevBoard[PACKED_BOARD_BYTES]; // for ko detection
    uint8_t turn;       // BLACK or WHITE
    uint8_t mode;
    uint8_t aiPlayer;
    uint8_t captures[2]; // captures[0]=black's captures, captures[1]=white's
    uint8_t consecutivePasses;
    uint8_t cursorX, cursorY;

    // Scoring
    int8_t territory[2]; // territory[0]=black, territory[1]=white
    uint8_t kpieces;      // komi in half-points (default 13 = 6.5)
    uint8_t resignedBy;   // 0 = none, else the color that resigned

    // Temp buffer for flood fill (values 0-2, packed like the boards)
    uint8_t visited[PACKED_BOARD_BYTES];

    void reset();
    uint8_t at(uint8_t x, uint8_t y);
    void set(uint8_t x, uint8_t y, uint8_t val);

    uint8_t isValidMove(uint8_t x, uint8_t y);
    uint8_t playMove(uint8_t x, uint8_t y);
    void pass();

    uint8_t hasLiberties(uint8_t start);
    uint8_t captureGroup(uint8_t start);
    uint8_t floodFill(uint8_t start, uint8_t color);
    uint8_t floodClean(uint8_t start, uint8_t color);

    void computeScore();
    uint8_t isGameOver();

    // Returns 1=black wins, 2=white wins
    uint8_t winner();
};
