// Ladder-reader probe. Build with/without -DLADDER_PRUNE and compare.
#include <cstdio>
#include <cstring>
#include "../game.cpp"
#include "../ai.cpp"
uint8_t Arduboy2Base::sBuffer[1024];

int main() {
    // 1. Driven ladder: B(2,2) in atari (W west, north, south, plus
    //    the driving stone at (3,1)); sole liberty (3,2). The chase
    //    zigzags SE to the edge -> B dies.
    memset(simBoard, EMPTY, sizeof(simBoard));
    simBoard[2*9+2] = BLACK;
    simBoard[2*9+1] = WHITE;   // (1,2)
    simBoard[1*9+2] = WHITE;   // (2,1)
    simBoard[3*9+2] = WHITE;   // (2,3)
    simBoard[1*9+3] = WHITE;   // (3,1)
    printf("driven ladder to edge:  escapes=%d (want 0)\n",
           ladderEscapes(2*9+2, 2*9+3));
    // 2. Ladder breaker on the diagonal path.
    simBoard[6*9+6] = BLACK;
    printf("ladder with breaker:    escapes=%d (want 1)\n",
           ladderEscapes(2*9+2, 2*9+3));
    // 3. The differentiator: one liberty connects to a strong friendly
    //    group (low empty-room, so the old heuristic chases the OTHER
    //    side and lets the connection out); with the driving stone at
    //    (4,1) the ladder still works if the attacker blocks the
    //    connect point first. Old heuristic: escape (wrong). Prune:
    //    must-block the 4+ lib side -> captured.
    memset(simBoard, EMPTY, sizeof(simBoard));
    simBoard[2*9+2] = BLACK;   // the hunted stone
    simBoard[2*9+1] = WHITE;   // (1,2)
    simBoard[1*9+2] = WHITE;   // (2,1)
    simBoard[3*9+2] = WHITE;   // (2,3)
    simBoard[1*9+3] = WHITE;   // (3,1) cap
    simBoard[1*9+4] = WHITE;   // (4,1) driving stone
    simBoard[4*9+2] = BLACK;   // (2,4) friendly wall...
    simBoard[4*9+3] = BLACK;   // (3,4)
    simBoard[4*9+4] = BLACK;   // (4,4)
    printf("connect-out (real escape): escapes=%d (want 1)\n",
           ladderEscapes(2*9+2, 2*9+3));
    return 0;
}
