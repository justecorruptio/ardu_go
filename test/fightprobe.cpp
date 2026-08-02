// Fighting-context classifier: replay an SGF to just before move N
// (1-based over ALL moves), then report the tactical context of a
// given point: contact?, adjacent chain liberty classes (both colors),
// weakest nearby chain within distance 2. Used to classify hunt
// blunders as fighting vs positional.
//   ./fightprobe <sgf> <moveNo> <vertex like F5> [vertex2...]
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "../game.cpp"
#include "../ai.cpp"
uint8_t Arduboy2Base::sBuffer[1024];
static int loadMoves(const char *path, uint8_t *mx, uint8_t *my, uint8_t *pass) {
    FILE *f = fopen(path, "r");
    if(!f) { fprintf(stderr, "no sgf\n"); exit(1); }
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
    int upto = atoi(argv[2]);   // opendiag mv is 0-based: board before SGF index mv
    if(upto > n) upto = n;
    Game game; game.reset();
    for(int i = 0; i < upto; i++) {
        if(mp[i]) game.pass(); else game.playMove(mx[i], my[i]);
    }
    unpackBoard(game);
    buildChainMap();
    uint8_t toMove = (upto % 2 == 0) ? BLACK : WHITE;
    for(int a = 3; a < argc; a++) {
        const char *v = argv[a];
        uint8_t x = (v[0] >= 'A' ? v[0] - 'A' : v[0] - 'a');
        if(x >= 8) x--;               // GTP skips I
        uint8_t y = 9 - atoi(v + 1);  // GTP row 1 = bottom = our y 8
        uint8_t pos = y * 9 + x;
        printf("%s mv%d pos=%d toMove=%s: ", v, upto, pos,
               toMove == BLACK ? "B" : "W");
        if(simBoard[pos] != EMPTY) { printf("OCCUPIED\n"); continue; }
        uint8_t contact = 0, fMin = 9, eMin = 9;
        uint8_t q;
        FOR_EACH_NEIGHBOR(q, pos) {
            uint8_t s = simBoard[q];
            if(s == EMPTY) continue;
            contact++;
            uint8_t l = LIBS_OF(chainId[q]);
            if(s == toMove) { if(l < fMin) fMin = l; }
            else            { if(l < eMin) eMin = l; }
        }
        // weakest chain within Chebyshev distance 2
        uint8_t near2F = 9, near2E = 9;
        uint8_t px = pos % 9, py = pos / 9;
        for(int8_t dy = -2; dy <= 2; dy++) for(int8_t dx = -2; dx <= 2; dx++) {
            int8_t nx = px + dx, ny = py + dy;
            if(nx < 0 || nx > 8 || ny < 0 || ny > 8) continue;
            uint8_t p2 = (uint8_t)(ny * 9 + nx);
            uint8_t s = simBoard[p2];
            if(s == EMPTY) continue;
            uint8_t l = LIBS_OF(chainId[p2]);
            if(s == toMove) { if(l < near2F) near2F = l; }
            else            { if(l < near2E) near2E = l; }
        }
        printf("contact=%u adjFriendLibs=%u adjEnemyLibs=%u "
               "near2FriendLibs=%u near2EnemyLibs=%u\n",
               contact, fMin, eMin, near2F, near2E);
    }
    return 0;
}
