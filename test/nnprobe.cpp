// NN-opening probe: replay an SGF to move N, call chooseMove (the NN
// opening path) and report whether it fires + (with -DNN_DEBUG) the
// nstones/temp it computed. For tuning the quiet-and-deep handoff gate.
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
    Game game; AI ai; game.reset(); ai.reset();
    for(int i = 0; i < upto; i++) {
        if(mp[i]) { game.pass(); ai.notifyPass(); }
        else { game.playMove(mx[i], my[i]); ai.notifyMove(mx[i], my[i]); }
    }
    uint8_t before[PACKED_BOARD_BYTES];
    memcpy(before, game.board, sizeof before);
    uint8_t fired = ai.chooseMove(game);
    const char *cols = "ABCDEFGHJ";
    if(fired) {
        int mv = -1;
        for(uint8_t i = 0; i < BOARD_CELLS; i++)
            if(packedGet(game.board, i) != EMPTY && packedGet(before, i) == EMPTY) mv = i;
        printf("upto=%d fired=1 move=%c%d\n", upto, cols[mv % 9], 9 - mv / 9);
    } else printf("upto=%d fired=0 (handed to search)\n", upto);
    return 0;
}
