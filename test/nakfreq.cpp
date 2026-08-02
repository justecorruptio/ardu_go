// Nakade firing-frequency probe: replay SGFs and count positions where
// loadRootBoard finds nakade vitals (build with -DNAKADE).
//   ./nakfreq <sgf> [<sgf> ...]
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../game.cpp"
#include "../ai.cpp"
uint8_t Arduboy2Base::sBuffer[1024];

static int loadMoves(const char *path, uint8_t *mx, uint8_t *my,
                     uint8_t *pass) {
    FILE *f = fopen(path, "r");
    if(!f) return -1;
    static char buf[8192];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    buf[n] = 0;
    fclose(f);
    int cnt = 0;
    for(char *p = buf; (p = strchr(p, ';')); p++) {
        if((p[1] == 'B' || p[1] == 'W') && p[2] == '[') {
            if(p[3] == ']') { pass[cnt] = 1; mx[cnt] = my[cnt] = 0; cnt++; }
            else { pass[cnt] = 0; mx[cnt] = p[3]-'a'; my[cnt] = p[4]-'a'; cnt++; }
        }
    }
    return cnt;
}

int main(int argc, char **argv) {
    long positions = 0, firing = 0, games = 0, gamesWithFire = 0;
    for(int a = 1; a < argc; a++) {
        static uint8_t mx[512], my[512], mp[512];
        int n = loadMoves(argv[a], mx, my, mp);
        if(n < 0) continue;
        games++;
        Game game;
        game.reset();
        int fired = 0;
        for(int i = 0; i < n; i++) {
            if(mp[i]) game.pass();
            else game.playMove(mx[i], my[i]);
            loadRootBoard(game);
            positions++;
            if(nNakVitals) {
                firing++;
                if(getenv("NAKV"))
                    printf("%s mv %d/%d vitals %d: %d %d\n", argv[a],
                           i + 1, n, nNakVitals, nakVitals[0],
                           nNakVitals > 1 ? nakVitals[1] : -1);
                if(!fired) { fired = 1; gamesWithFire++; }
            }
        }
    }
    printf("games %ld  positions %ld  nakade-firing positions %ld (%.2f%%)"
           "  games touched %ld (%.1f%%)\n",
           games, positions, firing, 100.0 * firing / (positions ? positions : 1),
           gamesWithFire, 100.0 * gamesWithFire / (games ? games : 1));
    return 0;
}
