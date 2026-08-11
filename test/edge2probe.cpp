// edge2 blindness probe: at each of the 99 line-2 blunder positions, run
// think() and report the ROOT'S VIEW of KataGo's line-2 move vs the played
// line-3+ move: in tree? latent? visits/q? rank? -> bucket the failure mode.
#include <stdio.h>
#include <string.h>
#include "../game.cpp"
#include "../ai.cpp"
#include "/tmp/edge2_events.h"
uint8_t Arduboy2Base::sBuffer[1024];

static void gtp(int8_t m, char *o) {
    if(m < 0) { strcpy(o, "pass"); return; }
    char c = 'A' + (m % 9) + ((m % 9) >= 8 ? 1 : 0);
    sprintf(o, "%c%d", c, 9 - (m / 9));
}

int main() {
    for(uint8_t e = 0; e < NEV; e++) {
        Game game; AI ai;
        game.reset(); ai.reset();
        forceThinkSeed = 777;
        for(uint8_t j = 0; j < E_N[e]; j++) {
            int8_t m = E_MOVES[e][j];
            if(m < 0) { game.pass(); ai.notifyPass(); }
            else { game.playMove(m % 9, m / 9); ai.notifyMove(m % 9, m / 9); }
        }
        ai.think(game);
        uint8_t x, y; int8_t chosen = -1;
        if(!ai.resigned && ai.bestMove(game, x, y)) chosen = y * 9 + x;
        // walk root children: find best + played + chosen stats, and best's visit rank
        uint16_t bV = 0, bW = 0, pV = 0, pW = 0, cV = 0, cW = 0;
        uint8_t bIn = 0, bLat = 0, pIn = 0;
        uint16_t vlist[85]; uint8_t nv = 0;
        for(uint8_t c = node(0).firstChild; c != 0xFF; c = node(c).nextSibling) {
            uint16_t v = nVisits(c);
            if(v >= POISONED) continue;
            uint8_t mv = node(c).move & 0x7F;
            uint8_t lat = (node(c).move & 0x80) ? 1 : 0;
            if(!lat && nv < 85) vlist[nv++] = v;
            if(mv == E_BEST[e]) { bIn = 1; bLat = lat; bV = v; bW = nWins(c); }
            if(mv == E_PLAYED[e]) { pIn = 1; pV = v; pW = nWins(c); }
            if(mv == (uint8_t)chosen) { cV = v; cW = nWins(c); }
        }
        uint8_t rank = 1;
        for(uint8_t i = 0; i < nv; i++) if(vlist[i] > bV) rank++;
        char bs[8], ps[8], cs[8];
        gtp((int8_t)E_BEST[e], bs); gtp((int8_t)E_PLAYED[e], ps); gtp(chosen, cs);
        const char *bucket;
        if(!bIn) bucket = "ABSENT";
        else if(bLat) bucket = "LATENT";
        else if(chosen == (int8_t)E_BEST[e]) bucket = "AGREES";  // probe picks it now
        else {
            unsigned bq = bV ? 100u * bW / bV : 0, cq = cV ? 100u * cW / cV : 0;
            if(bV * 4 < cV && bq >= cq) bucket = "STARVED";      // few visits, q fine
            else if(bq + 10 <= cq) bucket = "MISJUDGED";         // eval dislikes it
            else bucket = "RACE";                                // close, pick logic
        }
        printf("%-9s loss=%.0f%% best=%-4s(v%-3u w%-3u r%u%s) played=%-4s(v%-3u) "
               "chose=%-4s(v%-3u q%u%%) %s\n",
               E_NAME[e], 100.0 * E_LOSS[e], bs, bV, bW, rank, bLat ? " LAT" : "",
               ps, pV, cs, cV, cV ? 100u * cW / cV : 0, bucket);
        fflush(stdout);
    }
    return 0;
}
