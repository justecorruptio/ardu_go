// Generic blunder-position probe: replay a hunt_NNN.sgf, then run think()
// under T different RNG seeds and print the pick distribution. Used to
// verify root-selection changes (LCB race) flip a blundered pick without
// needing the in-game RNG state.
//   ./lcbprobe <sgf> <replayMoves> [trials=24]
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../game.cpp"
#include "../ai.cpp"
uint8_t Arduboy2Base::sBuffer[1024];

static int loadMoves(const char *path, uint8_t *mx, uint8_t *my,
                     uint8_t *pass) {
    FILE *f = fopen(path, "r");
    if(!f) { fprintf(stderr, "no sgf %s\n", path); exit(1); }
    static char buf[8192];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    buf[n] = 0;
    fclose(f);
    int cnt = 0;
    for(char *p = buf; (p = strchr(p, ';')); p++) {
        if((p[1] == 'B' || p[1] == 'W') && p[2] == '[') {
            if(p[3] == ']') { pass[cnt] = 1; mx[cnt] = my[cnt] = 0; cnt++; }
            else {
                pass[cnt] = 0;
                mx[cnt] = p[3] - 'a';
                my[cnt] = p[4] - 'a';
                cnt++;
            }
        }
    }
    return cnt;
}

int main(int argc, char **argv) {
    if(argc < 2) { fprintf(stderr, "usage: lcbprobe sgf [trials]\n"); return 1; }
    int upto = argc > 2 ? atoi(argv[2]) : 9999;
    int trials = argc > 3 ? atoi(argv[3]) : 24;
    static uint8_t mx[512], my[512], mp[512];
    int n = loadMoves(argv[1], mx, my, mp);
    if(n > upto) n = upto;
    int picks[83] = {0};
    for(int t = 0; t < trials; t++) {
        Game game; AI ai;
        game.reset(); ai.reset();
        rngState = (uint16_t)(t * 2731 + 999) | 1;
        for(int i = 0; i < n; i++) {
            if(mp[i]) { game.pass(); ai.notifyPass(); }
            else { game.playMove(mx[i], my[i]); ai.notifyMove(mx[i], my[i]); }
        }
        ai.think(game);
        uint8_t x, y;
        if(ai.bestMove(game, x, y)) picks[y * 9 + x]++;
        else picks[82]++;
    }
    printf("replayed %d moves; %d trials:\n", n, trials);
    for(int m = 0; m < 83; m++)
        if(picks[m]) {
            if(m < 81)
                printf("  %c%d: %d\n",
                       'A' + (m % 9) + ((m % 9) >= 8 ? 1 : 0), 9 - m / 9,
                       picks[m]);
            else
                printf("  %s: %d\n", m == 81 ? "PASS" : "pass2", picks[m]);
        }
    return 0;
}
