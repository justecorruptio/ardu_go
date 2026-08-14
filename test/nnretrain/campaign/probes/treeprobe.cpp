// Post-think tree anatomy: depth histogram, PV depth, pool saturation.
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
static int maxd = 0, nodes = 0, leaves = 0;
static void walk(uint8_t n, int d) {
    nodes++; if(d > maxd) maxd = d;
    int kids = 0;
    for(uint8_t c = node(n).firstChild; c != 0xFF; c = node(c).nextSibling) {
        if(node(c).move & 0x80) continue;   // latent
        kids++; walk(c, d + 1);
    }
    if(!kids) leaves++;
}
int main() {
    int sumpv=0, summax=0, sumsims=0, sumnodes=0;
    for(int trial = 0; trial < 8; trial++) {
        Game g; g.reset();
        for(uint8_t y = 0; y < 9; y++) for(uint8_t x = 0; x < 9; x++) {
            char c = rows[y][x];
            g.set(x, y, c == 'X' ? BLACK : c == 'O' ? WHITE : EMPTY);
        }
        memcpy(g.prevBoard, g.board, sizeof(g.board));
        g.turn = WHITE;
        AI ai; ai.reset();
        rngState = 12345 + trial * 999;
        ai.think(g);
        maxd = nodes = leaves = 0;
        walk(0, 0);
        // PV: follow max-visit child
        int pvd = 0; uint8_t cur = 0;
        while(node(cur).firstChild != 0xFF) {
            uint16_t bv = 0; uint8_t bc = 0xFF;
            for(uint8_t c = node(cur).firstChild; c != 0xFF; c = node(c).nextSibling) {
                if(node(c).move & 0x80) continue;
                uint16_t v = nVisits(c);
                if(v < POISONED && v > bv) { bv = v; bc = c; }
            }
            if(bc == 0xFF) break;
            cur = bc; pvd++;
        }
        sumpv += pvd; summax += maxd; sumsims += thinkSims; sumnodes += nodes;
    }
    printf("ANATOMY sims=%.0f nodes=%.0f maxDepth=%.1f pvDepth=%.2f\n",
           sumsims/8.0, sumnodes/8.0, summax/8.0, sumpv/8.0);
    return 0;
}
