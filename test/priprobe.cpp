// candidatePrior comparison at a replayed position: prints the prior of
// every empty cell, sorted, marking given vertices of interest.
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
    Game game; game.reset();
    uint8_t lastPos = 0xFF;
    for(int i = 0; i < upto; i++) {
        if(mp[i]) { game.pass(); lastPos = 0xFF; }
        else { game.playMove(mx[i], my[i]); lastPos = my[i]*9+mx[i]; }
    }
    uint8_t toMove = (upto % 2 == 0) ? BLACK : WHITE;
    rootTurn = toMove; simKomi = game.kpieces;
    rootLast = lastPos; rootKo = NO_KO; scoreMode = 0;
    loadRootBoard(game);
    uint8_t near[12];
    uint8_t anyStone = buildNearMask(near);
    buildChainMap();
    struct E { int8_t p; uint8_t pos; };
    E list[81]; int cnt = 0;
    for(uint8_t pos = 0; pos < 81; pos++) {
        if(simBoard[pos] != EMPTY) continue;
        uint8_t pb = pos >> 3, pm = bitMask(pos);
        uint8_t isFar = 0;
        if(anyStone && !(near[pb] & pm)) {
            if(!(pgm_read_byte(FAR_BITMAP + pb) & pm)) continue;
            isFar = 1;
        }
        if(isOwnEye(pos, toMove)) continue;
        list[cnt].p = candidatePrior(pos, toMove, lastPos, isFar);
        list[cnt].pos = pos; cnt++;
    }
    for(int i = 1; i < cnt; i++) for(int j = i; j && list[j].p > list[j-1].p; j--)
        { E t = list[j]; list[j] = list[j-1]; list[j-1] = t; }
    printf("toMove=%s last=%d; top priors:\n", toMove==BLACK?"B":"W", lastPos);
    const char *cols = "ABCDEFGHJ";
    for(int i = 0; i < cnt && i < 24; i++) {
        uint8_t pos = list[i].pos;
        printf("  %c%d=%+d", cols[pos%9], 9-pos/9, list[i].p);
        if((i+1)%8==0) printf("\n");
    }
    printf("\n");
    for(int a = 3; a < argc; a++) {
        const char *v = argv[a];
        uint8_t x = (v[0]>='A'? v[0]-'A' : v[0]-'a'); if(x >= 8) x--;
        uint8_t pos = (9 - atoi(v+1)) * 9 + x;
        for(int i = 0; i < cnt; i++) if(list[i].pos == pos)
            printf("  %s prior=%+d rank=%d/%d\n", v, list[i].p, i+1, cnt);
    }
    return 0;
}
