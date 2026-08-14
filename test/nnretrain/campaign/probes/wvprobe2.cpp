// W/V distribution v2: many seeds, several positions (built-in midgame
// + SGF prefixes at engine-contested points). Prints per-position
// summary: sims, root kids, top-child visits, PV depth, WR histogram.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define private public
#include "../game.cpp"
#include "../ai.cpp"
#include "../utils.cpp"
uint8_t Arduboy2Base::sBuffer[1024];
static const char rows[9][10] = {
    "....XO...","..X.XO.O.","....XO...","..X.XOO..","...X.XO..",
    "...O.XO..",".OX..O...","...OO.OO.",".O.O...O.",
};
static int wrbin[10], nodes, maxv, pvd;
static void walk(uint8_t n, int d) {
    for(uint8_t c = node(n).firstChild; c != 0xFF; c = node(c).nextSibling) {
        if(node(c).move & 0x80) continue;
        uint16_t v = nVisits(c);
        if(v < 3000) {
            nodes++;
            if((int)v > maxv) maxv = v;
            if(v >= 8) {
                int b = (int)(10.0 * nWins(c) / v); if(b > 9) b = 9;
                wrbin[b]++;
            }
        }
        walk(c, d + 1);
    }
}
static void pv() {
    uint8_t cur = 0; pvd = 0;
    while(node(cur).firstChild != 0xFF) {
        uint16_t bv = 0; uint8_t bc = 0xFF;
        for(uint8_t c = node(cur).firstChild; c != 0xFF; c = node(c).nextSibling) {
            if(node(c).move & 0x80) continue;
            uint16_t v = nVisits(c);
            if(v < 3000 && v > bv) { bv = v; bc = c; }
        }
        if(bc == 0xFF) break;
        cur = bc; pvd++;
    }
}
static char sgfbuf[8192];
static void setupPos(Game &g, AI &ai, int mode, int stopAt) {
    g.reset(); ai.reset();
    if(mode == 0) {
        for(uint8_t y = 0; y < 9; y++) for(uint8_t x = 0; x < 9; x++) {
            char c = rows[y][x];
            g.set(x, y, c == 'X' ? BLACK : c == 'O' ? WHITE : EMPTY);
        }
        memcpy(g.prevBoard, g.board, sizeof(g.board));
        g.turn = WHITE;
        return;
    }
    int n = 0;
    for(char *p = sgfbuf; (p = strchr(p, ';')); p++) {
        if((p[1] != 'B' && p[1] != 'W') || p[2] != '[') continue;
        if(p[3] == ']') break;
        g.playMove(p[3]-'a', p[4]-'a'); ai.notifyMove(p[3]-'a', p[4]-'a');
        if(++n >= stopAt) break;
    }
}
int main(int argc, char **argv) {
    FILE *f = fopen(argv[1], "r");
    sgfbuf[fread(sgfbuf, 1, sizeof sgfbuf - 1, f)] = 0; fclose(f);
    struct { int mode, stop; const char *name; } cases[] = {
        {0, 0,  "bench-midgame(won ~70%)"},
        {1, 10, "sgf@10 (engine eval ~51%)"},
        {1, 16, "sgf@16 (engine eval ~50%)"},
        {1, 22, "sgf@22 (engine eval ~57%)"},
    };
    for(auto &c : cases) {
        long sumv = 0, sumsims = 0, sumpv = 0, sumnodes = 0;
        int gbin[10]; memset(gbin, 0, sizeof(gbin));
        int T = 16;
        int allwr = 0;
        for(int t = 0; t < T; t++) {
            Game g; AI ai;
            setupPos(g, ai, c.mode, c.stop);
            rngState = (uint16_t)(1000 + t * 2711);
            ai.think(g);
            memset(wrbin, 0, sizeof(wrbin)); nodes = 0; maxv = 0;
            walk(0, 0); pv();
            sumv += maxv; sumsims += thinkSims; sumpv += pvd; sumnodes += nodes;
            for(int b = 0; b < 10; b++) { gbin[b] += wrbin[b]; allwr += wrbin[b]; }
        }
        printf("%-28s sims=%.0f nodes=%.0f topV=%.0f pvDepth=%.2f  WR:",
               c.name, (double)sumsims/T, (double)sumnodes/T, (double)sumv/T, (double)sumpv/T);
        for(int b = 0; b < 10; b++) printf(" %d", gbin[b] * 100 / (allwr ? allwr : 1));
        printf(" (deciles %%)\n");
    }
    return 0;
}
