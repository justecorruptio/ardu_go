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
    const uint8_t hoshi[][2] = {{2,2},{6,2},{4,4},{2,6},{6,6}};
    for(uint8_t i = 0; i < 5; i++) {
        uint8_t x = GRID_LEFT + hoshi[i][0] * CELL_SIZE;
        uint8_t y = GRID_TOP + hoshi[i][1] * CELL_SIZE;
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
    jay.smallPrintPgm(ix, 12, F("CAPTURES"), 1);
    jay.smallPrintPgm(ix, 18, F("B:"), 1);
    jay.smallPrint(ix + 8, 18, itoa(game.captures[0]), 1);
    jay.smallPrintPgm(ix, 24, F("W:"), 1);
    const char *wc = itoa(game.captures[1]);
    jay.smallPrint(ix + 8, 24, wc, 1);
    jay.smallPrintPgm(ix + 8 + 4 * strlen(wc) + 4, 24, F("+6.5"), 1);
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

    jay.smallPrintPgm(39, 28, F("PLAY VS AI"), 1);
    jay.smallPrintPgm(39, 34, F("PLAY VS HUMAN"), 1);
    jay.smallPrintPgm(39, 40, F("SHOW RULES"), 1);
    jay.smallPrintPgm(39, 46, F("INVERT SCREEN"), 1);

    jay.smallPrintPgm(
        30 + (jay.counter / 4) % 4,
        28 + menuCursor * 6, F(">"), 1
    );
}

void Display::renderHelp() {
    jay.largePrintPgm(49, 1, F("RULES"), 1);
    jay.drawFastHLine(49, 9, 29);
    jay.smallPrintPgm(1, 15, F("SURROUND TERRITORY WITH STONES."), 1);
    jay.smallPrintPgm(1, 21, F("CAPTURE BY REMOVING LIBERTIES."), 1);
    jay.smallPrintPgm(1, 27, F("NO SUICIDE. KO RULE APPLIES."), 1);
    jay.smallPrintPgm(1, 33, F("PASS WHEN NO GOOD MOVES."), 1);
    jay.smallPrintPgm(1, 39, F("TWO PASSES ENDS THE GAME."), 1);
    jay.smallPrintPgm(1, 45, F("SCORE = TERRITORY + CAPTURES."), 1);
    jay.smallPrintPgm(1, 51, F("WHITE GETS 6.5 KOMI."), 1);
}

void Display::renderScoring() {
    jay.smallPrintPgm(66, 1, F("SCORING"), 1);
    jay.smallPrintPgm(66, 13, F("B TERR:"), 1);
    jay.smallPrint(94, 13, itoa(game.territory[0]), 1);
    jay.smallPrintPgm(66, 19, F("B CAPT:"), 1);
    jay.smallPrint(94, 19, itoa(game.captures[0]), 1);
    jay.smallPrintPgm(66, 31, F("W TERR:"), 1);
    jay.smallPrint(94, 31, itoa(game.territory[1]), 1);
    jay.smallPrintPgm(66, 37, F("W CAPT:"), 1);
    jay.smallPrint(94, 37, itoa(game.captures[1]), 1);
    jay.smallPrintPgm(66, 43, F("W KOMI:"), 1);
    jay.smallPrintPgm(94, 43, F("6.5"), 1);
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
