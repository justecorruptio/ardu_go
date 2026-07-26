// Interactive terminal ArduGo: play against the real device AI
// natively, no flashing. Compiles the actual game/ai sources with the
// stubbed Arduino headers in this directory.
//
//   ./play        play Black (AI is White, like the device's VS AI)
//   ./play w      play White
//
// Moves: coordinates like e5 / E5. Commands: pass, undo, new, quit.
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <string>
#include <vector>

#include "../game.cpp"
#include "../ai.cpp"

uint8_t Arduboy2Base::sBuffer[1024];

static Game game;
static AI ai;
static int lastPos = -1; // last stone played, for the board marker

struct Snapshot { Game g; AI a; int last; };
static std::vector<Snapshot> history;

// GTP-style coords: columns A..J skipping I, row 1 at the bottom
static std::string vertex(int pos) {
    char col = 'A' + pos % BOARD_SIZE;
    if(col >= 'I') col++;
    char buf[8];
    snprintf(buf, sizeof buf, "%c%d", col, 9 - pos / BOARD_SIZE);
    return buf;
}

static bool parseVertex(const char *s, uint8_t &x, uint8_t &y) {
    char col = toupper(s[0]);
    if(col == 'I' || col < 'A' || col > 'J') return false;
    int row = atoi(s + 1);
    if(row < 1 || row > 9) return false;
    x = col - 'A' - (col > 'I' ? 1 : 0);
    y = 9 - row;
    return true;
}

static bool isHoshi(int x, int y) {
    return (x == 2 || x == 4 || x == 6) && (y == 2 || y == 4 || y == 6) &&
           !((x == 2 || x == 6) && y == 4) && !(x == 4 && (y == 2 || y == 6));
}

static void draw() {
    printf("\n    A B C D E F G H J\n");
    for(int y = 0; y < BOARD_SIZE; y++) {
        printf("  %d ", 9 - y);
        for(int x = 0; x < BOARD_SIZE; x++) {
            uint8_t c = game.at(x, y);
            char g = (c == BLACK) ? 'X' : (c == WHITE) ? 'O'
                   : isHoshi(x, y) ? '+' : '.';
            // lowercase marks the newest stone
            if(y * BOARD_SIZE + x == lastPos && c != EMPTY) g = tolower(g);
            printf("%c ", g);
        }
        printf("%d\n", 9 - y);
    }
    printf("    A B C D E F G H J\n");
    printf("  captures  B:%d  W:%d\n", game.captures[0], game.captures[1]);
}

// The AI ran think(); show what it believes. "position" is the
// average over all real playouts this search — seed-free and not
// max-biased, the calibrated number. "move" is the visit leader's
// win rate, which carries its optimistic prior seed by design.
// Neither reaches 0 in lost positions: playouts model the opponent
// as semi-random, and swindles genuinely work ~5-10% of the time.
static void showBelief() {
    if(!thinkSims) return;
    printf("  (position ~%d%%", (int)(100L * thinkSimWins / thinkSims));
    uint16_t bestV = 0, bestW = 0;
    for(uint8_t c = node(0).firstChild; c != 0xFF; c = node(c).nextSibling) {
        uint16_t v = nVisits(c);
        if(v >= POISONED || v <= bestV) continue;
        bestV = v;
        bestW = nWins(c);
    }
    if(bestV) printf(", move %d%%", (int)(100L * bestW / bestV));
    printf(")");
}

static void score() {
    game.computeScore();
    int b2 = game.territory[0] * 2 + game.captures[0] * 2;
    int w2 = game.territory[1] * 2 + game.captures[1] * 2 + game.kpieces;
    printf("\n  B: %d territory + %d captures = %d\n",
           game.territory[0], game.captures[0], b2 / 2);
    printf("  W: %d territory + %d captures + 6.5 komi = %d.5\n",
           game.territory[1], game.captures[1], w2 / 2);
    printf("  == %s WINS ==\n", game.winner() == BLACK ? "BLACK" : "WHITE");
}

static void newGame() {
    game.reset();
    ai.reset();
    history.clear();
    lastPos = -1;
}

int main(int argc, char **argv) {
    uint8_t humanColor = (argc > 1 && tolower(argv[1][0]) == 'w') ? WHITE : BLACK;
    srand(time(NULL));
    newGame();
    printf("ArduGo host build — you are %s. Moves like e5; pass, undo, new, quit.\n",
           humanColor == BLACK ? "Black (X)" : "White (O)");
    draw();

    while(1) {
        if(game.isGameOver()) {
            score();
            printf("  (new / quit)\n");
        }

        if(!game.isGameOver() && game.turn != humanColor) {
            // AI turn: book first, then search — the device flow
            uint8_t before[PACKED_BOARD_BYTES];
            memcpy(before, game.board, sizeof before);
            if(ai.chooseMove(game)) {
                for(uint8_t i = 0; i < BOARD_CELLS; i++)
                    if(packedGet(game.board, i) != EMPTY &&
                       packedGet(before, i) == EMPTY) lastPos = i;
                printf("\nAI plays %s (book)\n", vertex(lastPos).c_str());
            } else {
                clock_t t0 = clock();
                ai.think(game);
                if(ai.resigned) {
                    game.resignedBy = 3 - humanColor;
                    printf("\nAI RESIGNS — you win!  (new / quit)\n");
                    draw();
                    // resignedBy makes isGameOver-style handling moot:
                    // block further moves until new/quit
                    while(1) {
                        printf("> ");
                        fflush(stdout);
                        char line[64];
                        if(!fgets(line, sizeof line, stdin)) return 0;
                        if(strstr(line, "quit") || strstr(line, "q\n")) return 0;
                        if(strstr(line, "new")) { newGame(); draw(); break; }
                    }
                    continue;
                }
                uint8_t x, y;
                if(ai.bestMove(game, x, y)) {
                    game.playMove(x, y);
                    ai.notifyMove(x, y);
                    lastPos = y * BOARD_SIZE + x;
                    printf("\nAI plays %s  [%d ms]", vertex(lastPos).c_str(),
                           (int)((clock() - t0) * 1000 / CLOCKS_PER_SEC));
                    showBelief();
                    printf("\n");
                } else {
                    game.pass();
                    ai.notifyPass();
                    lastPos = -1;
                    printf("\nAI passes\n");
                }
            }
            draw();
            continue;
        }

        printf("%s> ", game.turn == BLACK ? "black" : "white");
        fflush(stdout);
        char line[64];
        if(!fgets(line, sizeof line, stdin)) break;
        char *cmd = line;
        while(isspace(*cmd)) cmd++;
        for(char *e = cmd + strlen(cmd); e > cmd && isspace(e[-1]);) *--e = 0;
        if(!*cmd) continue;

        if(!strcasecmp(cmd, "quit") || !strcasecmp(cmd, "q")) break;
        if(!strcasecmp(cmd, "new")) { newGame(); draw(); continue; }
        if(!strcasecmp(cmd, "undo")) {
            if(history.empty()) { printf("nothing to undo\n"); continue; }
            game = history.back().g;
            ai = history.back().a;
            lastPos = history.back().last;
            history.pop_back();
            draw();
            continue;
        }
        if(game.isGameOver()) { printf("game over — new or quit\n"); continue; }

        if(!strcasecmp(cmd, "pass")) {
            history.push_back({game, ai, lastPos});
            game.pass();
            ai.notifyPass();
            lastPos = -1;
            continue;
        }

        uint8_t x, y;
        if(!parseVertex(cmd, x, y)) {
            printf("? %s (want e.g. e5, or pass/undo/new/quit)\n", cmd);
            continue;
        }
        history.push_back({game, ai, lastPos});
        if(!game.playMove(x, y)) {
            history.pop_back();
            printf("illegal move\n");
            continue;
        }
        ai.notifyMove(x, y);
        lastPos = y * BOARD_SIZE + x;
    }
    return 0;
}
