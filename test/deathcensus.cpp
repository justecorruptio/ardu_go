// endgame-arc P4 census: at each group-death drop position (ArduGo stones that
// KataGo ownership says flip dead within 4 plies), what does the ENGINE believe?
// - ownVote (the scoreDead vote, the engine's honest ownership read): does it
//   call the dying stones alive?
// - think(): playout wr + margin mean at the same position.
// Output: one line per event -> aggregate the blindness census in python.
#include <stdio.h>
#include <string.h>
#include "../game.cpp"
#include "../ai.cpp"
#include "/tmp/death_events.h"
uint8_t Arduboy2Base::sBuffer[1024];

int main() {
    for(uint8_t e = 0; e < NEV; e++) {
        Game game; AI ai;
        game.reset(); ai.reset();
        forceThinkSeed = 4242;
        for(uint8_t j = 0; j < E_N[e]; j++) {
            int8_t m = E_MOVES[e][j];
            if(m < 0) { game.pass(); ai.notifyPass(); }
            else { game.playMove(m % 9, m / 9); ai.notifyMove(m % 9, m / 9); }
        }
        ai.think(game);
        // raw ownership vote (non-mutating): own[i] = black votes / SCORE_PLAYOUTS
        uint8_t own[BOARD_CELLS];
        ownVote(game, own);
        uint8_t ardu = E_ARDU[e];
        // dying group's liberties + engine belief
        uint32_t beliefSum = 0; uint8_t nc = E_NC[e];
        uint8_t libmask[BOARD_CELLS]; memset(libmask, 0, sizeof libmask);
        uint8_t libs = 0;
        for(uint8_t ci = 0; ci < nc; ci++) {
            uint8_t cell = (uint8_t)E_CELLS[e][ci];
            uint8_t bv = own[cell];
            beliefSum += (ardu == BLACK) ? bv : (SCORE_PLAYOUTS - bv);
            uint8_t x = cell % 9, y = cell / 9;
            const int8_t dx[] = {-1,1,0,0}, dy[] = {0,0,-1,1};
            for(uint8_t d = 0; d < 4; d++) {
                int nx = x + dx[d], ny = y + dy[d];
                if(nx < 0 || nx > 8 || ny < 0 || ny > 8) continue;
                uint8_t q = ny * 9 + nx;
                if(game.at(nx, ny) == EMPTY && !libmask[q]) { libmask[q] = 1; libs++; }
            }
        }
        unsigned belief = (unsigned)(100UL * beliefSum / ((uint32_t)nc * SCORE_PLAYOUTS));
        printf("%-9s drop=%-6.1f cells=%-2u libs=%-2u belief=%3u%%  wr=%3u%%  margin=%+.1f\n",
               E_NAME[e], (double)E_DROP[e], nc, libs, belief,
               thinkSims ? (unsigned)(100UL * thinkSimWins / thinkSims) : 0,
               thinkAvgMargin2 / 2.0);
        fflush(stdout);
    }
    return 0;
}
