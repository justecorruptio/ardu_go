// Handoff arbitration: what does MCTS pick at these opening positions?
// Same replay contract as nndump_probe; runs a full think() + bestMove.
// stdin: <id> <ntoks> <toks...>   stdout: MPICK <id> <pos|255>
#include <cstdio>
#include <cstring>
#include "../../game.cpp"
#include "../../ai.cpp"
uint8_t Arduboy2Base::sBuffer[1024];
static Game game;
static AI ai;
int main() {
    char id[64];
    int ntoks;
    while(scanf("%63s %d", id, &ntoks) == 2) {
        game.reset();
        ai.reset();
        for(int i = 0; i < ntoks; i++) {
            char tok[8];
            scanf("%7s", tok);
            if(tok[0] == 'P') { game.pass(); ai.notifyPass(); }
            else { game.playMove(tok[0]-'0', tok[1]-'0'); ai.notifyMove(tok[0]-'0', tok[1]-'0'); }
        }
        rngState = (uint16_t)(2654435761u * (uint32_t)(ntoks + 7919)) | 1;
        ai.think(game);
        uint8_t x, y;
        uint8_t got = ai.bestMove(game, x, y);
        printf("MPICK %s %u\n", id, got ? (unsigned)(y * 9 + x) : 255);
        fflush(stdout);
    }
    return 0;
}
