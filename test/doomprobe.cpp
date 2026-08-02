// Doomed-save probe: replay to move N, then for the AI's actual move,
// report the save-read picture: adjacent own low-lib chains, the
// ladder verdict for extending each, post-extension liberty geometry
// (diagonal pair = net shape), and enemy/friend outside liberties.
//   ./doomprobe <sgf> <mv> <vertex>
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
    int upto = atoi(argv[2]);
    if(upto > n) upto = n;
    Game game; game.reset();
    for(int i = 0; i < upto; i++) {
        if(mp[i]) game.pass(); else game.playMove(mx[i], my[i]);
    }
    unpackBoard(game);
    buildChainMap();
    uint8_t toMove = (upto % 2 == 0) ? BLACK : WHITE;
    const char *v = argv[3];
    uint8_t x = (v[0] >= 'A' ? v[0] - 'A' : v[0] - 'a');
    if(x >= 8) x--;
    uint8_t y = 9 - atoi(v + 1);
    uint8_t pos = y * 9 + x;
    printf("%s mv%d toMove=%s\n", v, upto, toMove == BLACK ? "B" : "W");
    uint8_t q;
    FOR_EACH_NEIGHBOR(q, pos) {
        if(simBoard[q] != toMove) continue;
        uint8_t l = LIBS_OF(chainId[q]);
        printf("  own chain @%d libs=%u", q, l);
        if(l == 1) {
            uint8_t sl = soleLiberty(q);
            printf(" soleLib=%d ladderEscapes(ext@%s)=%u", sl, v,
                   sl == pos ? ladderEscapes(q, pos) : 255);
        }
        // post-extension geometry: play the move, look at merged libs
        uint8_t save[81]; memcpy(save, simBoard, 81);
        if(simPlay(pos, toMove, NO_KO) != ILLEGAL) {
            uint8_t libs = groupLibsFind(pos);
            uint8_t l1 = glcL1, l2 = glcL2;
            printf(" -> postLibs=%u", libs);
            if(libs == 2) {
                int8_t dx = (int8_t)(l1 % 9) - (int8_t)(l2 % 9);
                int8_t dy = (int8_t)(l1 / 9) - (int8_t)(l2 / 9);
                uint8_t diag = (dx == 1 || dx == -1) && (dy == 1 || dy == -1);
                printf(" l1=%u l2=%u diagonalLibs=%u", l1, l2, diag);
                if(diag) {
                    // the net point: the 2x2 corner opposite the chain
                    uint8_t c1 = (uint8_t)((l1 / 9) * 9 + (l2 % 9));
                    uint8_t c2 = (uint8_t)((l2 / 9) * 9 + (l1 % 9));
                    printf(" corners=%u(%u),%u(%u)", c1, simBoard[c1],
                           c2, simBoard[c2]);
                }
            }
        }
        memcpy(simBoard, save, 81);
        printf("\n");
    }
    return 0;
}
