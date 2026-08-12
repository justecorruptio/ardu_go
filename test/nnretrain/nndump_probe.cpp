// NN retrain cache builder: replay a position, dump the C extractor's exact
// per-candidate feature indices (NN_DUMP path). Features come FROM the
// shipping extractor, so trainer/C parity holds by construction.
// Build with ship tier flags + -Dprivate=public; run with NN_DUMP=1 FORCE_NN=1.
// stdin:  <id> <ntoks> <tok...>    tok = "xy" digits, "PP" = pass; Black first.
// stdout: POS <id> turn=<B|W> stones=<n>
//         CAND ... (one per scored candidate, from ai.cpp's NN_DUMP hook)
//         ENDPOS <id> best=<idx|255>
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
            else {
                uint8_t x = tok[0] - '0', y = tok[1] - '0';
                game.playMove(x, y);
                ai.notifyMove(x, y);
            }
        }
        uint8_t ns = 0;
        for(uint8_t p = 0; p < 81; p++)
            if(packedGet(game.board, p) != EMPTY) ns++;
        printf("POS %s turn=%c stones=%u\n", id,
               game.turn == BLACK ? 'B' : 'W', ns);
        uint8_t x, y;
        uint8_t got = ai.nnOpeningMove(game, x, y);
        printf("ENDPOS %s best=%u\n", id, got ? (unsigned)(y * 9 + x) : 255);
        fflush(stdout);
    }
    return 0;
}
