// Chosen-move probe: replay an SGF to move N, think once from a fixed
// seed, print the move the engine would play. For A/B-ing admission or
// eval changes against a reference move list (KataGo corrections).
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
    uint16_t seed = argc > 3 ? (uint16_t)atoi(argv[3]) : 777;
    Game game; AI ai; game.reset(); ai.reset();
    for(int i = 0; i < upto; i++) {
        if(mp[i]) { game.pass(); ai.notifyPass(); }
        else { game.playMove(mx[i], my[i]); ai.notifyMove(mx[i], my[i]); }
    }
    memset(pool, 0xA5, sizeof(Node) * NODE_POOL_SB); // device honesty
    rngState = seed | 1;
    ai.think(game);
    uint8_t x, y;
    if(ai.resigned) { printf("resign\n"); return 0; }
    if(ai.bestMove(game, x, y)) {
        const char *cols = "ABCDEFGHJ";
        printf("%c%d\n", cols[x], 9 - y);
    } else printf("pass\n");
    return 0;
}
