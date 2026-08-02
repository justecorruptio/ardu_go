// isValidMove equivalence probe: replay SGFs, dump validity bitmap for
// every position. Diff the output across code versions.
#include <cstdio>
#include <cstring>
#include "../game.cpp"
#include "../ai.cpp"
uint8_t Arduboy2Base::sBuffer[1024];
static int loadMoves(const char *path, uint8_t *mx, uint8_t *my, uint8_t *pass) {
    FILE *f = fopen(path, "r");
    if(!f) return -1;
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
    for(int a = 1; a < argc; a++) {
        static uint8_t mx[512], my[512], mp[512];
        int n = loadMoves(argv[a], mx, my, mp);
        if(n < 0) continue;
        Game game; game.reset();
        for(int i = 0; i < n; i++) {
            if(mp[i]) game.pass(); else game.playMove(mx[i], my[i]);
            printf("%s:%d:", argv[a], i);
            for(uint8_t y = 0; y < 9; y++)
                for(uint8_t x = 0; x < 9; x++)
                    putchar(game.isValidMove(x, y) ? '1' : '0');
            putchar('\n');
        }
    }
    return 0;
}
