#include "display.h"
#include "utils.h"

Display::Display(Jaylib &_jay, Game &_game)
:jay(_jay), game(_game) {}

void Display::renderBoard() {
    // Draw grid lines
    for(uint8_t i = 0; i < BOARD_SIZE; i++) {
        jay.drawFastHLine(GRID_LEFT, GRID_TOP + i * CELL_SIZE, (BOARD_SIZE - 1) * CELL_SIZE + 1);
        jay.drawFastVLine(GRID_LEFT + i * CELL_SIZE, GRID_TOP, (BOARD_SIZE - 1) * CELL_SIZE + 1);
    }

    // Draw star points (hoshi) for 9x9 as 3x3 squares
    static const uint8_t PROGMEM hoshi[][2] = {{2,2},{6,2},{4,4},{2,6},{6,6}};
    for(uint8_t i = 0; i < 5; i++) {
        uint8_t x = GRID_LEFT + pgm_read_byte(&hoshi[i][0]) * CELL_SIZE;
        uint8_t y = GRID_TOP + pgm_read_byte(&hoshi[i][1]) * CELL_SIZE;
        for(int8_t dy = -1; dy <= 1; dy++)
            jay.drawFastHLine(x - 1, y + dy, 3);
    }

    // Draw stones
    for(uint8_t gy = 0; gy < BOARD_SIZE; gy++) {
        for(uint8_t gx = 0; gx < BOARD_SIZE; gx++) {
            uint8_t cell = game.at(gx, gy);
            if(cell == EMPTY) continue;
            uint8_t x = GRID_LEFT + gx * CELL_SIZE - 3;
            uint8_t y = GRID_TOP + gy * CELL_SIZE - 3;
            if(cell == BLACK) {
                jay.drawBand(x, y, GLYPH_STONE_FILLED, 7);
            } else {
                jay.drawBand(x, y, GLYPH_STONE_FILLED, 7, 0); // clear interior
                jay.drawBand(x, y, GLYPH_STONE_OUTLINE, 7);
            }
        }
    }

    // Last-move indicator: tiny "v" centered on the newest stone, in
    // inverted color so it reads on both stone colors. The newest stone
    // is the one present now but not in prevBoard (none after a pass).
    for(uint8_t i = 0; i < BOARD_CELLS; i++) {
        if(packedGet(game.board, i) == EMPTY ||
           packedGet(game.prevBoard, i) != EMPTY) continue;
        uint8_t x = GRID_LEFT + (i % BOARD_SIZE) * CELL_SIZE;
        uint8_t y = GRID_TOP + (i / BOARD_SIZE) * CELL_SIZE;
        uint8_t c = packedGet(game.board, i) == BLACK ? 0 : 1;
        jay.drawBand(x - 1, y, GLYPH_LASTMOVE, 3, c);
        break;
    }
}

void Display::renderCursor() {
    uint8_t x = GRID_LEFT + game.cursorX * CELL_SIZE;
    uint8_t y = GRID_TOP + game.cursorY * CELL_SIZE;
    // Wall-clock blink (~256 ms phase), NOT the frame counter: after
    // a blocking think() the frame scheduler catches up in a burst of
    // instant frames, which made a frame-based blink stutter.
    uint8_t blink = (uint8_t)(millis() >> 8) & 1;
    if(blink)
        jay.drawBand(x - 3, y - 3, GLYPH_CURSOR, 7);
}

void Display::renderInfo() {
    uint8_t ix = 66; // clear of the board: stones/cursor reach x=63
    // Turn indicator
    jay.smallPrintPgm(ix, 4, game.turn == BLACK ? F("BLACK TO PLAY")
                                                : F("WHITE TO PLAY"), 1);

    // Captures, 3 px under the turn line (which spans y=4..8)
    jay.smallPrintPgm(ix, 12, F("CAPTURES\nB:\nW:"), 1);
    jay.prNum(ix + 8, 18, game.captures[0]);
    uint8_t wx = jay.prNum(ix + 8, 24, game.captures[1]);
    jay.smallPrintPgm(wx + 4, 24, F("+6.5"), 1);
    jay.drawFastHLine(ix, 31, 59);

    // Controls hint: one line, 3 px margin below (font is 5 tall,
    // screen 64: 64 - 3 - 5 = 56)
    jay.drawFastHLine(ix, 52, 59);
    jay.smallPrintPgm(ix, 56, F("A:PLACE  B:PASS"), 1);
}

void Display::renderTitle(uint8_t menuCursor) {
    jay.largePrintPgm(46, 10, F("ARDUGO"), 1);
    jay.drawFastHLine(46, 18, 35);
    jay.drawFastHLine(50, 20, 27);

    jay.smallPrintPgm(39, 28,
        F("PLAY VS AI\nPLAY VS HUMAN\nSHOW RULES\nINVERT SCREEN"), 1);

    jay.smallPrintPgm(
        30 + (jay.counter / 4) % 4,
        28 + menuCursor * 6, F(">"), 1
    );
}

void Display::renderHelp() {
    jay.largePrintPgm(49, 1, F("RULES"), 1);
    jay.drawFastHLine(49, 9, 29);
    jay.smallPrintPgm(1, 15,
        F("SURROUND TERRITORY WITH STONES.\n"
          "CAPTURE BY REMOVING LIBERTIES.\n"
          "NO SUICIDE. KO RULE APPLIES.\n"
          "PASS WHEN NO GOOD MOVES.\n"
          "TWO PASSES ENDS THE GAME.\n"
          "SCORE = TERRITORY + CAPTURES.\n"
          "WHITE GETS 6.5 KOMI."), 1);
}

// Right-aligned number in the score table: the small font advances
// 4 px/char (last glyph 3 px wide), so a d-digit value starts at
// edge - 4d + 1 to end flush at `edge`.
static void scoreNum(Jaylib &jay, uint8_t edge, uint8_t y, uint16_t v) {
    uint8_t d = v >= 100 ? 3 : v >= 10 ? 2 : 1;
    jay.prNum(edge - 4 * d + 1, y, v);
}

// Score-table layout, table-driven: each smallPrintPgm call site cost
// ~16B of argument setup; one loop over a PROGMEM layout table renders
// the same pixels. Strings and coordinates unchanged from the hand
// -written version (screenshot-diff verified).
struct ScLabel { uint8_t x, y; const char *s; };
static const char SC_S0[] PROGMEM = "SCORING";
static const char SC_S1[] PROGMEM = "WHT";
static const char SC_S2[] PROGMEM = "BLK";
static const char SC_S3[] PROGMEM = "TERR";
static const char SC_S4[] PROGMEM = "CAPT";
static const char SC_S5[] PROGMEM = "KOMI";
static const char SC_S6[] PROGMEM = "TOTAL";
static const char SC_S7[] PROGMEM = "6.5";
static const char SC_S8[] PROGMEM = ".5";
static const ScLabel SC_LBLS[] PROGMEM = {
    {66, 1, SC_S0}, {96, 11, SC_S1}, {116, 11, SC_S2},
    {66, 19, SC_S3}, {66, 25, SC_S4}, {66, 31, SC_S5},
    {66, 39, SC_S6}, {96, 31, SC_S7},  // "6.5" at EW-10 = 96
    {100, 39, SC_S8},                  // ".5" at EW-6 = 100
};
// Right-aligned cells: {edge, y}; values filled from the game at
// matching indices below.
static const uint8_t SC_CELLS[][2] PROGMEM = {
    {106, 19}, {126, 19}, {106, 25}, {126, 25}, {99, 39}, {126, 39},
};
void Display::renderScoring() {
    // Komi (and the .5 in white's total) lives only in the white column.
    for(uint8_t i = 0; i < sizeof(SC_LBLS) / sizeof(SC_LBLS[0]); i++) {
        jay.smallPrintPgm(pgm_read_byte(&SC_LBLS[i].x),
                          pgm_read_byte(&SC_LBLS[i].y),
                          (const __FlashStringHelper *)
                              pgm_read_ptr(&SC_LBLS[i].s), 1);
    }
    jay.drawFastHLine(66, 37, 61);      // rule above the totals
    // white total is always x.5 (integer points + 6.5 komi)
    uint16_t vals[6] = {
        (uint16_t)game.territory[1], (uint16_t)game.territory[0],
        (uint16_t)game.captures[1], (uint16_t)game.captures[0],
        (uint16_t)(game.territory[1] + game.captures[1] + 6),
        (uint16_t)(game.territory[0] + game.captures[0]),
    };
    for(uint8_t i = 0; i < 6; i++)
        scoreNum(jay, pgm_read_byte(&SC_CELLS[i][0]),
                 pgm_read_byte(&SC_CELLS[i][1]), vals[i]);
}

void Display::renderGameOver() {
    if(game.resignedBy) {
        jay.drawPromptPgm(22, game.resignedBy == WHITE ? F("WHITE RESIGNS!")
                                                       : F("BLACK RESIGNS!"), 0);
        return;
    }
    uint8_t w = game.winner();
    jay.drawPromptPgm(22, w == BLACK ? F("BLACK WINS!") : F("WHITE WINS!"), 0);
}
