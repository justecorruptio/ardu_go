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
        ".........",
        "....X....",
        "...O..X..",
        ".........",
        "...O.X...",
        ".........",
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
    packedSet(game.prevBoard, 2 * 9 + 4, EMPTY); // B ec (4,2) last
    game.turn = WHITE;
    for(int t = 0; t < 10; t++) {
        srand(1000 + t * 7777);
        ai.reset();
        ai.think(game);
        uint8_t x = 0xFF, y = 0xFF;
        ai.bestMove(game, x, y);
        printf("(%d,%d) ", x, y);
    }
    printf("\n");
    return 0;
}

