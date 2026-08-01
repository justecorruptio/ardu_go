// Generic blunder-position probe: replay a hunt_NNN.sgf, then run think()
// under T different RNG seeds and print the pick distribution. Used to
// verify root-selection changes (LCB race) flip a blundered pick without
// needing the in-game RNG state.
//   ./lcbprobe <sgf> <replayMoves> [trials=24]
// With trials < 0: game-exact mode -- -trials is the GAME NUMBER; the
// engine RNG is seeded exactly as playGame does (Knuth hash) and the
// SGF prefix is replayed ONCE, reproducing the in-game think at the
// decision move bit-for-bit (valid for post-405213e hunt games).
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
    int gameExact = trials < 0 ? -trials : 0;
    if(gameExact) trials = 1;
    for(int t = 0; t < trials; t++) {
        Game game; AI ai;
        game.reset(); ai.reset();
        rngState = gameExact
            ? (uint16_t)(2654435761u * (uint32_t)(gameExact + 1) >> 16) | 1
            : (uint16_t)(t * 2731 + 999) | 1;
        uint8_t aiColor = (n % 2 == 0) ? BLACK : WHITE; // mover at decision
        for(int i = 0; i < n; i++) {
            uint8_t mvColor = (i % 2 == 0) ? BLACK : WHITE;
            if(gameExact && mvColor == aiColor) {
                // re-run the engine's own turns so the RNG stream at the
                // decision matches the in-game state; each re-derived
                // move must equal the SGF record or reproduction failed
                uint8_t x, y; uint8_t ok = 0;
                if(ai.chooseMove(game)) {
                    // chooseMove already played+notified internally
                    ok = !mp[i] && game.at(mx[i], my[i]) == mvColor;
                    if(!ok) { fprintf(stderr,
                        "REPRO FAIL (book) at move %d\n", i); return 1; }
                    continue;
                }
                ai.think(game);
                if(ai.bestMove(game, x, y)) {
                    ok = !mp[i] && x == mx[i] && y == my[i];
                    if(ok) { game.playMove(x, y); ai.notifyMove(x, y); }
                } else ok = mp[i] ? (game.pass(), ai.notifyPass(), 1) : 0;
                if(!ok) { fprintf(stderr,
                    "REPRO FAIL at move %d (got %c%d want %c%d)\n", i,
                    'A' + x, 9 - y, 'A' + mx[i], 9 - my[i]); return 1; }
                continue;
            }
            if(mp[i]) { game.pass(); ai.notifyPass(); }
            else { game.playMove(mx[i], my[i]); ai.notifyMove(mx[i], my[i]); }
        }
        ai.think(game);
        uint8_t x, y;
        if(ai.bestMove(game, x, y)) picks[y * 9 + x]++;
        else picks[82]++;
        if(gameExact) {
            printf("game-exact root (v/w/q, > = pick):\n");
            for(uint8_t c = node(0).firstChild; c != 0xFF;
                c = node(c).nextSibling) {
                uint16_t v = nVisits(c), w = nWins(c);
                uint8_t m = node(c).move & 0x7F;
                if((node(c).move & 0x80) || v < 15) continue;
                printf("  %c%c%d v=%3u q=%2u%%\n",
                       (m == (uint8_t)(y * 9 + x)) ? '>' : ' ',
                       'A' + (m % 9) + ((m % 9) >= 8 ? 1 : 0),
                       9 - m / 9, v, v ? 100 * w / v : 0);
            }
        }
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
