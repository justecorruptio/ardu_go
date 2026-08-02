// Loose-ladder validation: replay SGF to mv (0-based), PLAY the given
// save move, then read the merged group. Labels come from the hunt
// corpus (blunder saves should read DEAD, healthy saves ALIVE).
//   ./looseprobe <sgf> <mv> <vertex>
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
    for(int i = 0; i < upto; i++) { if(mp[i]) game.pass(); else game.playMove(mx[i], my[i]); }
    unpackBoard(game);
    uint8_t toMove = (upto % 2 == 0) ? BLACK : WHITE;
    const char *v = argv[3];
    uint8_t x = (v[0]>='A'? v[0]-'A' : v[0]-'a'); if(x >= 8) x--;
    uint8_t y = 9 - atoi(v + 1);
    uint8_t pos = y * 9 + x;
    if(simPlay(pos, toMove, NO_KO) == ILLEGAL) { printf("ILLEGAL\n"); return 1; }
    uint8_t libs[4];
    uint8_t nl = groupLibsList(pos, libs, 4);
    uint8_t alive = looseLadderAlive(pos);
    printf("%-30s %s mv%2d %s: postLibs=%u -> %s\n", argv[1], v, upto,
           toMove==BLACK?"B":"W", nl, alive ? "ALIVE" : "DEAD");
    return 0;
}
