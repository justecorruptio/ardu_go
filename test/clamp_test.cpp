#include <stdio.h>
#include <string.h>
#include "../game.cpp"
#include "../ai.cpp"
uint8_t Arduboy2Base::sBuffer[1024];
static Game game;
static AI ai;
int main() {
    const char *rows[9] = {
        ".........",
        "...O.....",
        "..O.OOX..",
        ".XXX..X..",
        "..OXX....",
        "..OO.X...",
        "....OX...",
        ".........",
        "........."
    };
    game.reset();
    for(uint8_t y = 0; y < 9; y++)
        for(uint8_t x = 0; x < 9; x++) {
            char c = rows[y][x];
            game.set(x, y, c == 'X' ? BLACK : c == 'O' ? WHITE : EMPTY);
        }
    memcpy(game.prevBoard, game.board, sizeof(game.board));
    packedSet(game.prevBoard, 2 * 9 + 6, EMPTY); // B gc (6,2) is last
    game.turn = WHITE;
    for(int t = 0; t < 5; t++) {
        srand(1000 + t * 7777);
        ai.reset();
        ai.think(game);
        uint8_t x = 0xFF, y = 0xFF;
        ai.bestMove(game, x, y);
        printf("trial %d: plays (%d,%d)  eval %u/%u\n", t, x, y,
               thinkSimWins, thinkSims);
        // stats for the clamp point (6,6)
        for(uint8_t c = node(0).firstChild; c != 0xFF; c = node(c).nextSibling)
            if(node(c).move == 6 * 9 + 6)
                printf("   clamp(6,6): %u/%u\n", nWins(c), nVisits(c));
    }
    return 0;
}

