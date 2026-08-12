// endgame-arc P1 probe: replay the top-10 phase0 bleed positions to the ply
// BEFORE the recorded drop move, evolving the dynamic-komi adaptation state by
// running think() before each of ArduGo's prefix moves (a cold think can't show
// vKomiWin — it ratchets across thinks). At the drop ply: report the chosen
// move vs the recorded blunder, the true eval, and the mean playout margin
// (thinkAvgMargin2). Build base vs -DVKOMI_WIN -DVKW_MAX=16 and diff.
#include <stdio.h>
#include <string.h>
#include "../game.cpp"
#include "../ai.cpp"
#include "/tmp/vkw_positions.h"
uint8_t Arduboy2Base::sBuffer[1024];

static void gtp(int8_t m, char *out) {
    if(m < 0) { strcpy(out, "pass"); return; }
    char col = 'A' + (m % 9) + ((m % 9) >= 8 ? 1 : 0);
    sprintf(out, "%c%d", col, 9 - (m / 9));
}

int main() {
    for(uint8_t p = 0; p < NPOS; p++) {
        Game game; AI ai;
        game.reset(); ai.reset();
        forceThinkSeed = 12345;          // deterministic thinks, both arms
        uint8_t ardu = P_ARDU[p];
        for(uint8_t j = 0; j < P_N[p]; j++) {
            int8_t m = P_MOVES[p][j];
            uint8_t mover = (j & 1) ? WHITE : BLACK;
            if(mover == ardu && j >= 2) ai.think(game);   // evolve vKomi state
            if(m < 0) { game.pass(); ai.notifyPass(); }
            else {
                game.playMove(m % 9, m / 9);
                ai.notifyMove(m % 9, m / 9);
            }
        }
        ai.think(game);
        uint8_t x = 0xFF, y = 0xFF;
        int8_t chosen = -1;
        if(!ai.resigned && ai.bestMove(game, x, y)) chosen = y * 9 + x;
        char cs[8], ds[8];
        gtp(chosen, cs); gtp(P_DROP[p], ds);
        unsigned vkw = 0;
#ifdef VKOMI_WIN
        vkw = vKomiWin;
#endif
        printf("%-8s evalB=%+6.1f  chose %-4s (recorded blunder %-4s)  "
               "wr=%3u%%  avgMargin=%+6.1f  vkw=%u%s\n",
               P_NAME[p], (double)P_EVAL[p], cs, ds,
               thinkSims ? (unsigned)(100UL * thinkSimWins / thinkSims) : 0,
               thinkAvgMargin2 / 2.0, vkw, ai.resigned ? "  RESIGNED" : "");
        fflush(stdout);
    }
    return 0;
}
