// Playout-belief ownership map: replay to mv, run N playouts from the
// position, print per-cell Black-ownership % and the raw winrate for
// the side to move. Shows WHERE the playouts think territory is.
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "../game.cpp"
#include "../ai.cpp"
uint8_t Arduboy2Base::sBuffer[1024];
static int loadMoves(const char *path, uint8_t *mx, uint8_t *my, uint8_t *pass) {
    FILE *f = fopen(path, "r");
    if(!f) { fprintf(stderr, "no sgf %s\n", path); exit(1); }
    static char buf[8192];
    size_t n = fread(buf, 1, sizeof buf - 1, f); buf[n] = 0; fclose(f);
    int cnt = 0;
    for(char *p = buf; (p = strchr(p, ';')); p++)
        if((p[1]=='B'||p[1]=='W') && p[2]=='[') {
            if(p[3]==']'){pass[cnt]=1;mx[cnt]=my[cnt]=0;cnt++;}
            else {pass[cnt]=0;mx[cnt]=p[3]-'a';my[cnt]=p[4]-'a';cnt++;}
        }
    return cnt;
}
int main(int argc, char **argv) {
    static uint8_t mx[512], my[512], mp[512];
    int n = loadMoves(argv[1], mx, my, mp);
    int upto = atoi(argv[2]); if(upto > n) upto = n;
    int N = argc > 3 ? atoi(argv[3]) : 500;
    Game game; game.reset();
    for(int i = 0; i < upto; i++) { if(mp[i]) game.pass(); else game.playMove(mx[i], my[i]); }
    uint8_t toMove = (upto % 2 == 0) ? BLACK : WHITE;
    rngState = 12345;
    // root context like think(): load board, vitals
    rootTurn = toMove;
    simKomi = game.kpieces;
    rootLast = 0xFF; rootKo = NO_KO;
    scoreMode = 0;
    loadRootBoard(game);
    static uint32_t ownB[81]; memset(ownB, 0, sizeof(ownB));
    uint32_t wins = 0;
    static uint8_t rootCopy[81];
    memcpy(rootCopy, simBoard, 81);
    for(int t = 0; t < N; t++) {
        memcpy(simBoard, rootCopy, 81);
        memset(raveMask, 0, sizeof(raveMask));
        cacheLibsPos = 0xFF;
        uint8_t winner = playout(toMove, NO_KO, 0xFF);
        if(winner == toMove) wins++;
        for(uint8_t i = 0; i < 81; i++) {
            uint8_t s = simBoard[i];
            if(s == BLACK) ownB[i]++;
            else if(s == EMPTY) {
                // empty at playout end: owner = bordering color (crude)
                uint8_t q, b = 0, w = 0;
                FOR_EACH_NEIGHBOR(q, i) {
                    if(simBoard[q] == BLACK) b = 1;
                    if(simBoard[q] == WHITE) w = 1;
                }
                if(b && !w) ownB[i]++;
            }
        }
    }
    printf("toMove=%s rawWinrate=%.1f%% over %d playouts\n",
           toMove==BLACK?"B":"W", 100.0*wins/N, N);
    printf("Black-ownership map (root stones as letters):\n");
    for(uint8_t y = 0; y < 9; y++) {
        for(uint8_t x = 0; x < 9; x++) {
            uint8_t i = y*9+x;
            uint8_t rs = rootCopy[i];
            if(rs == BLACK) printf("  X");
            else if(rs == WHITE) printf("  O");
            else printf(" %2u", (unsigned)(100*ownB[i]/N/10));
        }
        printf("  %u\n", 9-y);
    }
    printf("  A  B  C  D  E  F  G  H  J   (digits = B-own x10%%)\n");
    return 0;
}
