// endgame-arc owl-lite validation: run owlHopeless on the 32 census death
// events (KataGo ground truth: ALL these groups die). DEAD-recall is the
// power metric; the control set (survivor groups) measures false-DEAD.
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "../game.cpp"
#include "../ai.cpp"
#ifndef EV_HEADER
#define EV_HEADER "/tmp/death_events.h"
#endif
#include EV_HEADER
uint8_t Arduboy2Base::sBuffer[1024];

int main() {
    unsigned hopeless = 0, skipped = 0;
    for(uint8_t e = 0; e < NEV; e++) {
        Game game; AI ai;
        game.reset(); ai.reset();
        for(uint8_t j = 0; j < E_N[e]; j++) {
            int8_t m = E_MOVES[e][j];
            if(m < 0) { game.pass(); ai.notifyPass(); }
            else { game.playMove(m % 9, m / 9); ai.notifyMove(m % 9, m / 9); }
        }
        loadRootBoard(game);
        uint8_t seed = (uint8_t)E_CELLS[e][0];
        if(simBoard[seed] != E_ARDU[e]) { skipped++; continue; }
        clock_t t0 = clock();
        uint8_t h = owlHopeless(seed);
        double ms = 1000.0 * (clock() - t0) / CLOCKS_PER_SEC;
        hopeless += h;
        printf("%-9s drop=%-6.1f cells=%-2u  owl=%s  nodes=%-4u  %.1fms\n",
               E_NAME[e], (double)E_DROP[e], E_NC[e],
               h ? "HOPELESS" : "alive?  ", owlNodes, ms);
    }
    printf("\nDEAD-recall: %u/%u hopeless (%u skipped)\n",
           hopeless, NEV - skipped, skipped);
    return 0;
}
