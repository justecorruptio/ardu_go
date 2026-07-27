// Search benchmark: loop think() on a realistic mid-game position.
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "../game.cpp"
#include "../ai.cpp"
uint8_t Arduboy2Base::sBuffer[1024];
static Game game;
static AI ai;
int main(int argc, char **argv) {
    const char *rows[9] = {
        "....XO...",
        "..X.XO.O.",
        "....XO...",
        "..X.XOO..",
        "...X.XO..",
        "...O.XO..",
        ".OX..O...",
        "...OO.OO.",
        ".O.O...O.",
    };
    game.reset();
    for(uint8_t y = 0; y < 9; y++)
        for(uint8_t x = 0; x < 9; x++) {
            char c = rows[y][x];
            game.set(x, y, c == 'X' ? BLACK : c == 'O' ? WHITE : EMPTY);
        }
    memcpy(game.prevBoard, game.board, sizeof(game.board));
    game.turn = WHITE;
    int iters = argc > 1 ? atoi(argv[1]) : 300;
    srand(42);
    clock_t t0 = clock();
    for(int i = 0; i < iters; i++) {
        ai.reset();
        ai.think(game);
    }
    double ms = (double)(clock() - t0) * 1000 / CLOCKS_PER_SEC / iters;
    printf("%.2f ms/think (%d thinks)\n", ms, iters);
    return 0;
}
