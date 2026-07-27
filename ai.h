#pragma once

#include "game.h"

class AI {
    public:
    void reset();

    // Call for every stone placed (either player) to advance the book walk
    void notifyMove(uint8_t x, uint8_t y);
    void notifyPass();

    // Opening book. Returns 1 if a move was made, 0 if off book.
    uint8_t chooseMove(Game &game);

    // Blocking MCTS once off book. Borrows the screen buffer as the
    // node pool — draw and display() a "thinking" frame first (the OLED
    // retains it), and redraw after. Then read bestMove (0 = pass).
    // If think() sets `resigned`, the game is hopeless: skip bestMove
    // and end the game as a resignation.
    void think(Game &game);
    uint8_t bestMove(Game &game, uint8_t &x, uint8_t &y);

    // Game-over scoring: playout-vote dead stones off the board
    // (mutates game: removes them as captures), then computeScore.
    void scoreDead(Game &game);
    uint8_t resigned;

    // Filled by bestMove for the info panel: chosen move's win rate
    // (percent) and visits, plus the search's total simulations.
    uint8_t statPct;
    uint16_t statVisits;
    uint16_t statTotal;

    private:
    uint16_t bookPos[8]; // child-list offset per symmetry candidate
    uint8_t bookAlive;   // bitmask of surviving candidates
    uint8_t firstMove;   // 1 until any stone has been played
    uint8_t passToWin;   // set by think(): pass now to end a won game
    uint8_t resignCount; // consecutive hopeless searches (strict tier)
    uint8_t resignCount2; // consecutive bad searches (mild tier)

    uint16_t trieSkip(uint16_t p);
    int16_t trieFindChild(uint16_t p, uint8_t moveIdx);
    uint8_t bookLookup(uint8_t &x, uint8_t &y);
};
