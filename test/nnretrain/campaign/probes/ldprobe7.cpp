// Ply-2 node X-ray: after a think at the blunder position, find the
// kataBest child and dump its children/latents + whether the reply is
// weakLib-flagged on THAT node's board.
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
        rngState = 0x5A5A;
        ai.think(g);
        // locate kataBest child of root
        uint8_t kb = 0xFF;
        for(uint8_t c = node(0).firstChild; c != 0xFF; c = node(c).nextSibling)
            if(!(node(c).move & 0x80) && node(c).move == (uint8_t)pvm[0]) { kb = c; break; }
        printf("%s t=%d reply=%d kataBest=%d ", path + 13, turn, pvm[1], pvm[0]);
        if(kb == 0xFF) { printf("KB-NOT-CHILD\n"); continue; }
        printf("kbV=%u kids:", nVisits(kb));
        for(uint8_t c = node(kb).firstChild; c != 0xFF; c = node(c).nextSibling)
            printf(" %d%s(V%u)", node(c).move & 0x7F,
                   (node(c).move & 0x80) ? "L" : "", nVisits(c) < 3000 ? nVisits(c) : 0);
        // rebuild that node's board + chain map to inspect the bitmap
        unpackBoard(g);
        simPlay((uint8_t)pvm[0], (uint8_t)(g.turn), rootKo);
        buildChainMap();
        uint8_t r = (uint8_t)pvm[1];
        printf(" | replyFlagged=%d\n", (weakLibs[r >> 3] & bitMask(r)) ? 1 : 0);
    }
    return 0;
}
