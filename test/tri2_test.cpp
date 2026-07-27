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
        "....X....",
        "....OX...",
        "...O..X..",
        ".........",
        "...O.....",
        ".....X...",
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
    packedSet(game.prevBoard, 1 * 9 + 4, EMPTY); // B eb (4,1) last
    game.turn = WHITE;
    int dc = 0, e6 = 0;
    for(int t = 0; t < 10; t++) {
        srand(1000 + t * 7777);
        ai.reset();
        ai.think(game);
        uint8_t x = 0xFF, y = 0xFF;
        ai.bestMove(game, x, y);
        printf("(%d,%d) ", x, y);
        if(x == 3 && y == 2) dc++;
        if(x == 4 && y == 3) e6++;
    }
    printf("\ntriangle D7: %d/10   solid-side E6: %d/10\n", dc, e6);
    return 0;
}

