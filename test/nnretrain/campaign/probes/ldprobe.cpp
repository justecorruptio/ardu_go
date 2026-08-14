// L&D-blunder tree diagnostic: at each blunder position (SGF prefix,
// engine to move), run a CURRENT-engine think and report how the tree
// sees the ship's played blunder vs KataGo's best move.
//   ldprobe <list.txt>   lines: <sgfpath> <turn> <playedPos> <bestPos>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define private public
#include "../game.cpp"
#include "../ai.cpp"
#include "../utils.cpp"
uint8_t Arduboy2Base::sBuffer[1024];
static char sgfbuf[8192];
static int pvDepth() {
    uint8_t cur = 0; int d = 0;
    while(node(cur).firstChild != 0xFF) {
        uint16_t bv = 0; uint8_t bc = 0xFF;
        for(uint8_t c = node(cur).firstChild; c != 0xFF; c = node(c).nextSibling) {
            if(node(c).move & 0x80) continue;
            uint16_t v = nVisits(c);
            if(v < 3000 && v > bv) { bv = v; bc = c; }
        }
        if(bc == 0xFF) break;
        cur = bc; d++;
    }
    return d;
}
static void childStat(uint8_t mv, uint16_t &v, int &wrp) {
    v = 0; wrp = -1;
    for(uint8_t c = node(0).firstChild; c != 0xFF; c = node(c).nextSibling) {
        if(node(c).move & 0x80 || node(c).move != mv) continue;
        uint16_t vv = nVisits(c);
        if(vv >= 3000) { v = 0; wrp = -2; return; }  // poisoned
        v = vv;
        if(vv) wrp = (int)(100UL * nWins(c) / vv);
        return;
    }
}
int main(int argc, char **argv) {
    FILE *lf = fopen(argv[1], "r");
    char path[256]; int turn, played, best;
    while(fscanf(lf, "%255s %d %d %d", path, &turn, &played, &best) == 4) {
        FILE *f = fopen(path, "r");
        if(!f) { printf("MISSING %s\n", path); continue; }
        sgfbuf[fread(sgfbuf, 1, sizeof sgfbuf - 1, f)] = 0; fclose(f);
        Game g; g.reset(); AI ai; ai.reset();
        int n = 0; uint8_t ok = 1;
        for(char *p = sgfbuf; n < turn && (p = strchr(p, ';')); p++) {
            if((p[1] != 'B' && p[1] != 'W') || p[2] != '[') continue;
            if(p[3] == ']') { g.pass(); ai.notifyPass(); n++; continue; }
            if(!g.playMove(p[3]-'a', p[4]-'a')) { ok = 0; break; }
            ai.notifyMove(p[3]-'a', p[4]-'a');
            n++;
        }
        if(!ok || n < turn) { printf("REPLAYFAIL %s t=%d\n", path, turn); continue; }
        rngState = 0x5A5A;
        ai.think(g);
        uint8_t px, py; uint8_t got = ai.bestMove(g, px, py);
        int pick = got ? py * 9 + px : -1;
        uint16_t pv_, bv_; int pw, bw;
        childStat((uint8_t)played, pv_, pw);
        childStat((uint8_t)best, bv_, bw);
        printf("g=%s t=%d sims=%u pvD=%d pick=%d played=%d best=%d "
               "playedV=%u playedWR=%d bestV=%u bestWR=%d pickIsPlayed=%d pickIsBest=%d\n",
               path, turn, thinkSims, pvDepth(), pick, played, best,
               pv_, pw, bv_, bw, pick == played, pick == best);
    }
    return 0;
}
