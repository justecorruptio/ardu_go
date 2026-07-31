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

void Display::renderScoring() {
    // Label column in one string: blank lines make the y=1->13 and
    // 19->31 section gaps (12 px = 2 lines)
    jay.smallPrintPgm(66, 1,
        F("SCORING\n\nB TERR:\nB CAPT:\n\nW TERR:\nW CAPT:\nW KOMI:"), 1);
    jay.prNum(94, 13, game.territory[0]);
    jay.prNum(94, 19, game.captures[0]);
    jay.prNum(94, 31, game.territory[1]);
    jay.prNum(94, 37, game.captures[1]);
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
