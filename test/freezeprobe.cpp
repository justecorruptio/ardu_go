// Reproduce the play_gui freeze: replay Jay's saved game, then do exactly
// what the GUI does when the human passes -- think (passToWin path),
// bestMove, pass, scoreDead. An alarm converts a hang into a hard exit
// so the probe reports where it stopped.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include "../game.cpp"
#include "../ai.cpp"
uint8_t Arduboy2Base::sBuffer[1024];

static const char *STAGE = "init";
static void die(int) {
    fprintf(stderr, "HANG at stage: %s\n", STAGE);
    _exit(2);
}

static int runOne(const char *path, uint16_t seed);

int main(int argc, char **argv) {
    signal(SIGALRM, die);
    int sweep = argc > 3 ? atoi(argv[3]) : 0;
    if(sweep) {
        for(int t = 0; t < sweep; t++) {
            uint16_t seed = (uint16_t)(t * 2731 + 7) | 1;
            fprintf(stderr, "\rseed %5d/%d (0x%04X)", t, sweep, seed);
            alarm(atoi(argv[2]));
            runOne(argv[1], seed);
        }
        fprintf(stderr, "\nSWEEP CLEAN\n");
        return 0;
    }
    alarm(argc > 2 ? atoi(argv[2]) : 20);
    return runOne(argv[1], 0xC772 | 1);
}

static int runOne(const char *path, uint16_t seed) {
    const char *argv1 = path;
    FILE *f = fopen(argv1, "r");
    static char buf[8192];
    buf[fread(buf, 1, sizeof buf - 1, f)] = 0;
    fclose(f);
    Game game; AI ai;
    game.reset(); ai.reset();
    rngState = seed;
    int n = 0;
    for(char *p = buf; (p = strchr(p, ';')); p++) {
        if((p[1] != 'B' && p[1] != 'W') || p[2] != '[') continue;
        if(p[3] == ']') { break; }   // stop at the human's pass
        if(getenv("EXACT_SEED") && !strncmp(p+1, "W[ca]", 5) &&
           strstr(p, "C772")) break;  // stop BEFORE the AI's last move
        uint8_t x = p[3] - 'a', y = p[4] - 'a';
        game.playMove(x, y);
        ai.notifyMove(x, y);
        n++;
    }
    printf("replayed %d stones; turn=%s consecutivePasses=%d\n", n,
           game.turn == BLACK ? "B" : "W", game.consecutivePasses);
    // Re-run the AI's LAST move as a real think (the GUI state at the
    // freeze includes the previous think's tree in the pool; replay-only
    // left poolUsed=0 and skipped the stale-tree walk entirely).
    // The last SGF entry replayed was the AI's W C9 -- undo it from the
    // replay is impossible, so instead: the replay loop above stopped
    // BEFORE the human pass; the tree state comes from thinking the
    // position where the AI chose its last move. Fake it: think now on
    // the current position (builds a tree of the same shape class).
    STAGE = "pre-think (tree builder)";
    // Exact-fidelity: undoing isn't possible, so main() stops the replay
    // ONE move earlier when EXACT_LAST is set, and we re-derive the AI's
    // final move with its recorded seed -- bit-identical tree to the GUI.
    if(getenv("EXACT_SEED")) {
        forceThinkSeed = (uint16_t)strtol(getenv("EXACT_SEED"), NULL, 16);
        ai.think(game);
        forceThinkSeed = 0;
        uint8_t ex, ey;
        if(ai.bestMove(game, ex, ey)) {
            printf("re-derived last AI move: %c%d\n",
                   'A' + ex + (ex >= 8), 9 - ey);
            fflush(stdout);
            game.playMove(ex, ey);
            ai.notifyMove(ex, ey);
        } else printf("re-derived last AI move: PASS?!\n");
        {
            int steps = 0;
            for(uint8_t c = node(0).firstChild; c != 0xFF && steps < 4096;
                c = node(c).nextSibling) steps++;
            printf("post-think-40 walk: %s (%d)\n",
                   steps >= 4096 ? "CYCLE ALREADY" : "clean", steps);
            fflush(stdout);
        }
    } else
        ai.think(game);   // builds a stale tree for the walk below
    // the human passes
    STAGE = "human pass";
    game.pass();
    ai.notifyPass();
    // GUI: aiMoveIfNeeded -> think
    STAGE = "think";
    ai.think(game);
    printf("think done: resigned=%u\n", ai.resigned);
    STAGE = "bestMove";
    uint8_t x, y;
    uint8_t got = ai.bestMove(game, x, y);
    printf("bestMove=%u\n", got);
    if(!got) {
        STAGE = "ai pass";
        game.pass();
        ai.notifyPass();
    }
    STAGE = "treeTable walk";
    {
        // exact GUI loop, bounded: >4096 sibling steps = cycle
        int steps = 0;
        for(uint8_t c = node(0).firstChild; c != 0xFF;
            c = node(c).nextSibling) {
            uint16_t v = nVisits(c);
            (void)v;
            if(++steps > 4096) {
                fprintf(stderr, "CYCLE in stale sibling list! dump:\n");
                int k = 0;
                for(uint8_t d = node(0).firstChild; k < 30;
                    d = node(d).nextSibling, k++)
                    fprintf(stderr, "  [%d] node %u mv=%u lat=%u vis=%u\n",
                            k, d, node(d).move & 0x7F, node(d).move >> 7,
                            nVisits(d));
                _exit(3);
            }
        }
        printf("treeTable walk ok (%d siblings, poolUsed=%u thinkSims=%u)\n",
               steps, poolUsed, thinkSims);
    }
    STAGE = "scoreDead";
    ai.scoreDead(game);
    printf("scoreDead done: B %d+%d  W %d+%d\n",
           game.territory[0], game.captures[0],
           game.territory[1], game.captures[1]);
    STAGE = "done";
    printf("NO HANG\n");
    return 0;
}
