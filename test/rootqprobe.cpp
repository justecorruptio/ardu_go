// Root q-spread probe: replay ALL sgf moves to N, think once from a
// fixed seed, dump the root children (move, visits, q). The
// calibration metric for reward changes: does the top-move q clump
// spread by margin-robustness?
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
    rngState = seed | 1;
    ai.think(game);
    const char *cols = "ABCDEFGHJ";
    int cnt = 0; double sum = 0, sum2 = 0;
    for(uint8_t c = node(0).firstChild; c != 0xFF; c = node(c).nextSibling) {
        Node &nd = node(c);
        if(nd.move & 0x80) continue;
        uint16_t v = nRefVisits(nd);
        if(v >= POISONED || v < 15) continue;
        double q = 100.0 * nRefWins(nd) / v;
        if(nd.move < 81)
            printf("  %c%d v=%3u q=%.0f%%\n", cols[nd.move % 9],
                   9 - nd.move / 9, v, q);
        cnt++; sum += q; sum2 += q * q;
    }
    if(cnt > 1) {
        double mean = sum / cnt;
        printf("children(v>=15)=%d mean_q=%.1f stddev_q=%.1f\n",
               cnt, mean, __builtin_sqrt(sum2 / cnt - mean * mean));
    }
    return 0;
}
