#include <stdio.h>
#include <string.h>
#include "../game.cpp"
#include "../ai.cpp"
uint8_t Arduboy2Base::sBuffer[1024];
static Game game;
static AI ai;
// does a WHITE stone at (x,y) complete an empty triangle here?
static int makesTri(uint8_t px, uint8_t py) {
    for(int8_t dy = -1; dy <= 1; dy += 2)
        for(int8_t dx = -1; dx <= 1; dx += 2) {
            int8_t fx = px + dx, fy = py + dy;
            if(fx < 0 || fx > 8 || fy < 0 || fy > 8) continue;
            uint8_t sd = simBoard[fy * 9 + fx];
            uint8_t s1 = simBoard[py * 9 + fx];
            uint8_t s2 = simBoard[fy * 9 + px];
            if(sd == WHITE && ((s1 == WHITE && s2 == EMPTY) ||
                               (s2 == WHITE && s1 == EMPTY))) return 1;
            if(sd == EMPTY && s1 == WHITE && s2 == WHITE) return 1;
        }
    return 0;
}
int main() {
    const char *rows[9] = {
        ".........",
        ".........",
        ".........",
        "...OX.X..",
        "..O.X....",
        "...O.....",
        "..O..X...",
        "...X.....",
        "........."
    };
    game.reset();
    for(uint8_t y = 0; y < 9; y++)
        for(uint8_t x = 0; x < 9; x++) {
            char c = rows[y][x];
            game.set(x, y, c == 'X' ? BLACK : c == 'O' ? WHITE : EMPTY);
        }
    memcpy(game.prevBoard, game.board, sizeof(game.board));
    packedSet(game.prevBoard, 3 * 9 + 4, EMPTY); // B ed (4,3) is last
    game.turn = WHITE;
    int tris = 0;
    for(int t = 0; t < 10; t++) {
        srand(1000 + t * 7777);
        ai.reset();
        ai.think(game);
        uint8_t x = 0xFF, y = 0xFF;
        ai.bestMove(game, x, y);
        for(uint8_t i = 0; i < BOARD_CELLS; i++)
            simBoard[i] = packedGet(game.board, i);
        int tri = makesTri(x, y);
        tris += tri;
        printf("(%d,%d)%s ", x, y, tri ? "*TRI*" : "");
    }
    printf("\n%d/10 empty-triangle picks\n", tris);
    return 0;
}

