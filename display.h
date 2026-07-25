#pragma once

#include "jaylib.h"
#include "game.h"
#include "glyphs.h"

#define GRID_LEFT 4
#define GRID_TOP 4
#define CELL_SIZE 7

class Display {
    public:
    Jaylib &jay;
    Game &game;

    Display(Jaylib &jay, Game &game);

    void renderBoard();
    void renderCursor();
    void renderInfo();
    void renderTitle(uint8_t menuCursor);
    void renderHelp();
    void renderScoring();
    void renderGameOver();
};
