// Tiger's-mouth prior probe: Jay's SGF (B dd, W de, B ee, W pass) with
// candidate fd completing the mouth over ed. Build twice (with and
// without -DNO_TIGER); the per-candidate diff isolates the tiger term.
#include <cstdio>
#include <cstring>
#include "../game.cpp"
#include "../ai.cpp"
uint8_t Arduboy2Base::sBuffer[1024];
int main(){
    memset(simBoard, EMPTY, sizeof(simBoard));
    simBoard[3*9+3] = BLACK;  // dd = D6
    simBoard[4*9+3] = WHITE;  // de = D5
    simBoard[4*9+4] = BLACK;  // ee = E5
    uint8_t last = 4*9+4;
    struct { const char *n; uint8_t x, y; } c[] = {
        {"fd F6 (completes E6 mouth)",     5, 3},
        {"ed E6 (the mouth point itself)", 4, 3},
        {"ff F4 (nearby, no mouth)",       5, 5},
        {"ec E7 (above the mouth)",        4, 2},
        {"cd C6 (west, no mouth)",         2, 3},
    };
    for(unsigned i = 0; i < sizeof(c)/sizeof(c[0]); i++) {
        uint8_t pos = c[i].y * 9 + c[i].x;
        printf("%-34s prior=%+d\n", c[i].n,
               candidatePrior(pos, BLACK, last, 0));
    }
    return 0;
}
