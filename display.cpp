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
        jay.drawPixel(x - 1, y, c);
        jay.drawPixel(x + 1, y, c);
        jay.drawPixel(x, y + 1, c);
        break;
    }
}

void Display::renderCursor() {
    uint8_t x = GRID_LEFT + game.cursorX * CELL_SIZE;
    uint8_t y = GRID_TOP + game.cursorY * CELL_SIZE;
    uint8_t blink = (jay.counter >> 3) & 1;
    if(blink) {
        // Top-left corner
        jay.drawPixel(x - 3, y - 3, 1);
        jay.drawPixel(x - 2, y - 3, 1);
        jay.drawPixel(x - 3, y - 2, 1);
        // Top-right corner
        jay.drawPixel(x + 3, y - 3, 1);
        jay.drawPixel(x + 2, y - 3, 1);
        jay.drawPixel(x + 3, y - 2, 1);
        // Bottom-left corner
        jay.drawPixel(x - 3, y + 3, 1);
        jay.drawPixel(x - 2, y + 3, 1);
        jay.drawPixel(x - 3, y + 2, 1);
        // Bottom-right corner
        jay.drawPixel(x + 3, y + 3, 1);
        jay.drawPixel(x + 2, y + 3, 1);
        jay.drawPixel(x + 3, y + 2, 1);
    }
}

void Display::renderInfo() {
    uint8_t ix = 66; // clear of the board: stones/cursor reach x=63
    // Turn indicator
    jay.smallPrint(ix, 1, game.turn == BLACK ? "BLACK" : "WHITE", 1);
    jay.smallPrint(ix, 7, "TO PLAY", 1);

    // Captures
    jay.smallPrint(ix, 19, "CAPTURES", 1);
    jay.smallPrint(ix, 25, "B:", 1);
    jay.smallPrint(ix + 8, 25, itoa(game.captures[0]), 1);
    jay.smallPrint(ix, 31, "W:", 1);
    jay.smallPrint(ix + 8, 31, itoa(game.captures[1]), 1);

    // Controls hint
    jay.smallPrint(ix, 49, "A:PLACE", 1);
    jay.smallPrint(ix, 55, "B:PASS", 1);
}

void Display::renderTitle(uint8_t menuCursor) {
    jay.largePrint(46, 10, "ARDUGO", 1);
    jay.drawFastHLine(46, 18, 35);
    jay.drawFastHLine(50, 20, 27);

    jay.smallPrint(39, 28, "PLAY VS AI", 1);
    jay.smallPrint(39, 34, "PLAY VS HUMAN", 1);
    jay.smallPrint(39, 40, "SHOW RULES", 1);
    jay.smallPrint(39, 46, "INVERT SCREEN", 1);

    jay.smallPrint(
        30 + (jay.counter / 4) % 4,
        28 + menuCursor * 6, ">", 1
    );
}

void Display::renderHelp() {
    jay.largePrint(49, 1, "RULES", 1);
    jay.drawFastHLine(49, 9, 29);
    jay.smallPrint(1, 15, "SURROUND TERRITORY WITH STONES.", 1);
    jay.smallPrint(1, 21, "CAPTURE BY REMOVING LIBERTIES.", 1);
    jay.smallPrint(1, 27, "NO SUICIDE. KO RULE APPLIES.", 1);
    jay.smallPrint(1, 33, "PASS WHEN NO GOOD MOVES.", 1);
    jay.smallPrint(1, 39, "TWO PASSES ENDS THE GAME.", 1);
    jay.smallPrint(1, 45, "SCORE = TERRITORY + CAPTURES.", 1);
    jay.smallPrint(1, 51, "WHITE GETS 6.5 KOMI.", 1);
}

void Display::renderScoring() {
    jay.smallPrint(66, 1, "SCORING", 1);
    jay.smallPrint(66, 13, "B TERR:", 1);
    jay.smallPrint(94, 13, itoa(game.territory[0]), 1);
    jay.smallPrint(66, 19, "B CAPT:", 1);
    jay.smallPrint(94, 19, itoa(game.captures[0]), 1);
    jay.smallPrint(66, 31, "W TERR:", 1);
    jay.smallPrint(94, 31, itoa(game.territory[1]), 1);
    jay.smallPrint(66, 37, "W CAPT:", 1);
    jay.smallPrint(94, 37, itoa(game.captures[1]), 1);
    jay.smallPrint(66, 43, "W KOMI:", 1);
    jay.smallPrint(94, 43, "6.5", 1);
}

void Display::renderGameOver() {
    if(game.resignedBy) {
        jay.drawPrompt(22, game.resignedBy == WHITE ? "WHITE RESIGNS!"
                                                    : "BLACK RESIGNS!", 0);
        return;
    }
    uint8_t w = game.winner();
    jay.drawPrompt(22, w == BLACK ? "BLACK WINS!" : "WHITE WINS!", 0);
}
