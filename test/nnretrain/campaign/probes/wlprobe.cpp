// weakLibs bitmap diagnosis: at each blunder position (parent think
// context), how many cells are flagged, and is the critical reply one?
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define private public
#include "../game.cpp"
#include "../ai.cpp"
#include "../utils.cpp"
uint8_t Arduboy2Base::sBuffer[1024];
int main(int argc, char **argv) {
    FILE *lf = fopen(argv[1], "r");
    char lbuf[512];
    while(fgets(lbuf, sizeof lbuf, lf)) {
        char path[256]; int turn, played, best;
        int pvm[8], npv = 0, off = 0, k;
        if(sscanf(lbuf, "%255s %d %d %d%n", path, &turn, &played, &best, &off) < 4) continue;
        { const char *q = lbuf + off;
          while(npv < 8 && sscanf(q, "%d%n", &pvm[npv], &k) == 1) { q += k; npv++; } }
        if(npv < 2) continue;
        FILE *f = fopen(path, "r");
        static char sgfbuf[8192];
        sgfbuf[fread(sgfbuf, 1, sizeof sgfbuf - 1, f)] = 0; fclose(f);
        Game g; g.reset(); AI ai; ai.reset();
        int n = 0;
        for(char *p = sgfbuf; n < turn && (p = strchr(p, ';')); p++) {
            if((p[1] != 'B' && p[1] != 'W') || p[2] != '[') continue;
            if(p[3] == ']') { g.pass(); ai.notifyPass(); n++; continue; }
            g.playMove(p[3]-'a', p[4]-'a'); ai.notifyMove(p[3]-'a', p[4]-'a');
            n++;
        }
        loadRootBoard(g);   // builds weakLibs for the PARENT position
        int flagged = 0;
        for(uint8_t c = 0; c < BOARD_CELLS; c++)
            if(weakLibs[c >> 3] & bitMask(c)) flagged++;
        uint8_t r = (uint8_t)pvm[1];
        int rIn = (weakLibs[r >> 3] & bitMask(r)) ? 1 : 0;
        int adj = 0; uint8_t q;
        FOR_EACH_NEIGHBOR(q, r) if(weakLibs[q >> 3] & bitMask(q)) adj++;
        printf("%s t=%d reply=%d flagged=%d replyFlagged=%d replyAdj=%d\n",
               path + 13, turn, r, flagged, rIn, adj);
    }
    return 0;
}
