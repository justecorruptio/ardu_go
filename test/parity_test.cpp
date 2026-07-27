// Chain-map parity: on positions sampled from real playouts, every
// stone's chainLibs class must equal the flood-computed class, and
// id-equality must equal flooded same-chain membership.
#include <stdio.h>
#include <string.h>
#include "../game.cpp"
#include "../ai.cpp"
uint8_t Arduboy2Base::sBuffer[1024];
int main() {
    srand(7);
    rngState = 1234;
    int checked = 0;
    for(int game = 0; game < 40; game++) {
        // random-fill a position via the playout engine itself
        memset(simBoard, EMPTY, sizeof(simBoard));
        cacheLibsPos = 0xFF;
        rootTurn = BLACK;
        simKomi = 13;
        memset(raveMask, 0, sizeof(raveMask));
        nRootVitals = 0;
        // play a partial playout to get a messy mid-game board
        uint8_t save = 0;
        (void)save;
        uint8_t toMove = BLACK, ko = NO_KO, last = 0xFF;
        for(int m = 0; m < 30 + (rnd16() % 40); m++) {
            uint8_t pos = rnd(BOARD_CELLS);
            uint8_t tries = 0;
            while(tries++ < 81) {
                if(simBoard[pos] == EMPTY && pos != ko &&
                   !isOwnEye(pos, toMove)) {
                    uint8_t nk = simPlay(pos, toMove, ko, 0);
                    if(nk != ILLEGAL) { ko = nk; last = pos; break; }
                }
                pos = (pos + 1) % BOARD_CELLS;
            }
            toMove = 3 - toMove;
        }
        buildChainMap();
        for(uint8_t s = 0; s < BOARD_CELLS; s++) {
            if(simBoard[s] == EMPTY) {
                if(chainId[s] != 0) { printf("FAIL empty id %d\n", s); return 1; }
                continue;
            }
            uint8_t truth = groupLibsMax3(s);
            uint8_t mapl = LIBS_OF(chainId[s]);
            if(mapl == 0) mapl = 1; // groupLibsMax3 reads 0 as 1
            if(truth != mapl) {
                printf("FAIL libs at %d: map %d truth %d\n", s, mapl, truth);
                return 1;
            }
            // membership: same id iff flood from s marks it
            newMark();
            groupLibsMark(s);
            for(uint8_t t = 0; t < BOARD_CELLS; t++) {
                if(simBoard[t] == EMPTY) continue;
                uint8_t sameFlood = (simMark[t] == markEpoch &&
                                     simBoard[t] == simBoard[s]);
                uint8_t sameId = (CHAIN_OF(chainId[t]) == CHAIN_OF(chainId[s]));
                if(sameFlood != sameId) {
                    printf("FAIL membership %d vs %d\n", s, t);
                    return 1;
                }
            }
            checked++;
        }
    }
    printf("parity OK: %d stones checked across 40 boards\n", checked);
    return 0;
}
