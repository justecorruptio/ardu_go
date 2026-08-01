// throwaway: verify CAPSIZE_PRIOR fires on a 2-stone atari capture
#include <stdio.h>
#include <string.h>
#include "../game.cpp"
#include "../ai.cpp"
uint8_t Arduboy2Base::sBuffer[1024];
int main() {
    memset(simBoard, EMPTY, sizeof(simBoard));
    memset(chainId, 0, sizeof(chainId));
    memset(regionDone, 0, sizeof(regionDone));
    // White pair E5,F5 (40,41); Black D5,G5,E6,F6,F4 leave E4(31) as
    // the pair's last liberty. Black E4 captures two stones.
    simBoard[40] = WHITE; simBoard[41] = WHITE;
    simBoard[39] = BLACK; simBoard[42] = BLACK;
    simBoard[49] = BLACK; simBoard[50] = BLACK;
    simBoard[32] = BLACK;
    rootStones = 7;
    buildChainMap();
    printf("prior(E4, captures 2) = %+d\n", candidatePrior(31, BLACK, 41, 0));
    // Lone white stone in atari for comparison
    memset(simBoard, EMPTY, sizeof(simBoard));
    memset(chainId, 0, sizeof(chainId));
    memset(regionDone, 0, sizeof(regionDone));
    simBoard[40] = WHITE;
    simBoard[39] = BLACK; simBoard[41] = BLACK; simBoard[49] = BLACK;
    rootStones = 4;
    buildChainMap();
    printf("prior(E4, captures 1) = %+d\n", candidatePrior(31, BLACK, 40, 0));
    return 0;
}
