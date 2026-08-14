// Root-vs-deep discriminator: replay prefix + KataGo's best move, then
// think from THAT position (ply-2 node becomes root). Does the critical
// reply (kata pv[1]) surface among root children/latents now?
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
        // play KataGo's best (pv[0]) as a real move; think as the replier
        if(!g.playMove(pvm[0] % 9, pvm[0] / 9)) { printf("BADPV0 %s t=%d\n", path, turn); continue; }
        ai.notifyMove(pvm[0] % 9, pvm[0] / 9);
        rngState = 0x5A5A;
        ai.think(g);
        // scan root children incl latents for pv[1]
        int rank = 0, foundRank = -1, isLat = 0; uint16_t vv = 0;
        for(uint8_t c = node(0).firstChild; c != 0xFF; c = node(c).nextSibling) {
            rank++;
            if((node(c).move & 0x7F) == (uint8_t)pvm[1]) {
                foundRank = rank; isLat = (node(c).move & 0x80) ? 1 : 0;
                vv = isLat ? 0 : nVisits(c);
                break;
            }
        }
        uint8_t px, py; uint8_t got = ai.bestMove(g, px, py);
        printf("g=%s t=%d reply=%d atRoot=%s rank=%d latent=%d V=%u rootPick=%d "
               "pickIsReply=%d top:",
               path, turn, pvm[1], foundRank > 0 ? "YES" : "NO", foundRank, isLat,
               vv, got ? py*9+px : -1, got && (py*9+px) == pvm[1]);
        { int shown = 0;
          for(uint8_t c = node(0).firstChild; c != 0xFF && shown < 12;
              c = node(c).nextSibling) {
              if(node(c).move == MOVE_PASS) continue;
              printf(" %d", node(c).move & 0x7F); shown++;
          } }
        printf("\n");
    }
    return 0;
}
