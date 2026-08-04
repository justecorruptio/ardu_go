// Batch pattern-score emitter for NN training features.
// stdin lines: "B|W p1 p2 ... pn L" — toMove, stone list as pos*2+isWhite
// tokens, L = count. For each line prints 81 space-separated
// patternBonus values (occupied cells print 99).
#include <cstdio>
#include <cstring>
#include <cstdlib>
#define private public
#include "../game.cpp"
#include "../ai.cpp"
uint8_t Arduboy2Base::sBuffer[1024];
int main() {
    char line[1024];
    while(fgets(line, sizeof line, stdin)) {
        char *tok = strtok(line, " \n");
        if(!tok) continue;
        uint8_t toMove = (tok[0] == 'B' || tok[0] == 'b') ? BLACK : WHITE;
        memset(simBoard, EMPTY, BOARD_CELLS);
        while((tok = strtok(NULL, " \n"))) {
            int v = atoi(tok);
            uint8_t pos = v >> 1;
            if(pos < 81) simBoard[pos] = (v & 1) ? WHITE : BLACK;
        }
        for(uint8_t p = 0; p < 81; p++) {
            if(simBoard[p] != EMPTY) { printf("99 "); continue; }
            printf("%d ", (int)patternBonus(p % 9, p / 9, toMove));
        }
        printf("\n");
        fflush(stdout);
    }
    return 0;
}
