// Book-exit probe: replay an SGF through the AI's book tracking and
// print the ply at which the book died (bookAlive == 0), or -1 if it
// survived to the given depth. Also prints which move killed it and
// whether it was a first-line kill.
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
    int upto = argc > 2 ? atoi(argv[2]) : 20; if(upto > n) upto = n;
    AI ai; ai.reset();
    ai.firstMove = 0; // replaying: track only, never emit
    for(int i = 0; i < upto; i++) {
        if(mp[i]) { ai.notifyPass(); }
        else ai.notifyMove(mx[i], my[i]);
        if(!ai.bookAlive) {
            uint8_t firstLine = !mp[i] &&
                (mx[i]==0 || mx[i]==BOARD_SIZE-1 || my[i]==0 || my[i]==BOARD_SIZE-1);
            printf("%d %s %s\n", i, firstLine ? "line1" : (mp[i] ? "pass" : "offbook"),
                   mp[i] ? "--" : "");
            return 0;
        }
    }
    printf("-1 alive --\n");
    return 0;
}
