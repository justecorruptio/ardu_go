// Net (geta) prior probe. Build with and without -DNET; the diff per
// candidate isolates the net term. W to move everywhere.
#include <cstdio>
#include <cstring>
#include "../game.cpp"
#include "../ai.cpp"
uint8_t Arduboy2Base::sBuffer[1024];

static void show(const char *n, uint8_t pos) {
    buildChainMap();
    printf("%-46s prior=%+d\n", n, candidatePrior(pos, WHITE, 0xFF, 0));
}
int main() {
    // 1. Classic net: B(3,3) libs exactly {(4,3),(3,4)}; cap at (4,4).
    memset(simBoard, EMPTY, sizeof(simBoard));
    simBoard[3*9+3] = BLACK;
    simBoard[3*9+2] = WHITE;  // (2,3)
    simBoard[2*9+3] = WHITE;  // (3,2)
    show("net point (4,4) of 2-lib B(3,3)", 4*9+4);
    // 2. Same but B has 3 libs: no net.
    simBoard[2*9+3] = EMPTY;
    show("same but B has 3 libs", 4*9+4);
    // 3. 2-lib B but a flank occupied: no net.
    simBoard[2*9+3] = WHITE;
    simBoard[3*9+4] = WHITE;  // flank (4,3) occupied -> B in atari too
    show("flank occupied (B in atari)", 4*9+4);
    // 4. Empty board control.
    memset(simBoard, EMPTY, sizeof(simBoard));
    simBoard[3*9+3] = BLACK;
    simBoard[4*9+4] = BLACK;  // unrelated far stone to defeat isFar-ish
    show("no 2-lib chain nearby", 5*9+5);
    return 0;
}
