// Dump per-node (depth, visits, wins) after a 1000-iter think.
#include <stdio.h>
#include <string.h>
#define private public
#include "../game.cpp"
#include "../ai.cpp"
#include "../utils.cpp"
uint8_t Arduboy2Base::sBuffer[1024];
static const char rows[9][10] = {
    "....XO...","..X.XO.O.","....XO...","..X.XOO..","...X.XO..",
    "...O.XO..",".OX..O...","...OO.OO.",".O.O...O.",
};
static void walk(uint8_t n, int d) {
    if(d) printf("%d %u %u\n", d, nVisits(n), nWins(n));
    for(uint8_t c = node(n).firstChild; c != 0xFF; c = node(c).nextSibling) {
        if(node(c).move & 0x80) continue;
        walk(c, d + 1);
    }
}
int main() {
    for(int trial = 0; trial < 4; trial++) {
        Game g; g.reset();
        for(uint8_t y = 0; y < 9; y++) for(uint8_t x = 0; x < 9; x++) {
            char c = rows[y][x];
            g.set(x, y, c == 'X' ? BLACK : c == 'O' ? WHITE : EMPTY);
        }
        memcpy(g.prevBoard, g.board, sizeof(g.board));
        g.turn = WHITE;
        AI ai; ai.reset();
        rngState = 4242 + trial * 1111;
        ai.think(g);
        walk(0, 0);
    }
    return 0;
}
