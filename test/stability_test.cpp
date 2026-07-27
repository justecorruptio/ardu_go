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
    for(int t = 0; t < 20; t++) {
        srand(1000 + t * 7777);
        ai.reset();
        ai.think(game);
        uint8_t x = 0xFF, y = 0xFF;
        ai.bestMove(game, x, y);
        printf("(%d,%d) ", x, y);
    }
    return 0;
}

