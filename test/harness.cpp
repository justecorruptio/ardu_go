// Strength-test harness: plays the ardu_go AI against GNU Go over GTP.
// Compiles the real game/ai sources natively (Arduboy2.h stubbed).
//
// Usage: ./harness <games> <gnugo-level> [iterations-override]
// Writes one SGF per game plus a summary to stdout.
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <string>
#include <vector>

#include "../game.cpp"
#include "../ai.cpp"

uint8_t Arduboy2Base::sBuffer[1024];

// ---------- GTP subprocess ----------
static FILE *gtpIn, *gtpOut;
static pid_t gtpPid;

static void gtpStart(int level, int seed) {
    int inPipe[2], outPipe[2];
    pipe(inPipe);
    pipe(outPipe);
    gtpPid = fork();
    if(gtpPid == 0) {
        dup2(inPipe[0], 0);
        dup2(outPipe[1], 1);
        close(inPipe[1]); close(outPipe[0]);
        char lvl[16], sd[16];
        snprintf(lvl, sizeof lvl, "%d", level);
        snprintf(sd, sizeof sd, "%d", seed);
        execlp("gnugo", "gnugo", "--mode", "gtp", "--boardsize", "9",
               "--komi", "6.5", "--level", lvl, "--seed", sd,
               "--chinese-rules", (char *)NULL);
        _exit(127);
    }
    close(inPipe[0]); close(outPipe[1]);
    gtpIn = fdopen(inPipe[1], "w");
    gtpOut = fdopen(outPipe[0], "r");
}

static void gtpStop() {
    fclose(gtpIn); fclose(gtpOut);
    kill(gtpPid, SIGTERM);
    waitpid(gtpPid, NULL, 0);
}

// Send a command, return the response line (without "= " / "? ").
// Returns false on "?" (error) responses.
static bool gtpCmd(const std::string &cmd, std::string &resp) {
    fprintf(gtpIn, "%s\n", cmd.c_str());
    fflush(gtpIn);
    char buf[256];
    resp.clear();
    bool ok = true, first = true;
    while(fgets(buf, sizeof buf, gtpOut)) {
        std::string line(buf);
        while(!line.empty() && (line.back() == '\n' || line.back() == '\r'))
            line.pop_back();
        if(line.empty()) {
            if(!first) break; // blank line terminates a response
            continue;
        }
        if(first) {
            ok = line[0] == '=';
            line = line.substr(line.size() > 1 && line[1] == ' ' ? 2 : 1);
            first = false;
        }
        if(!resp.empty()) resp += "\n";
        resp += line;
    }
    return ok;
}

// ---------- coordinate helpers ----------
// GTP: columns A..J skipping I, row 1 = bottom. Ours: x 0-8, y 0 = top.
static std::string toVertex(uint8_t x, uint8_t y) {
    char col = 'A' + x;
    if(col >= 'I') col++;
    char buf[8];
    snprintf(buf, sizeof buf, "%c%d", col, 9 - y);
    return buf;
}

static bool fromVertex(const std::string &v, uint8_t &x, uint8_t &y) {
    if(v.size() < 2) return false;
    char col = toupper(v[0]);
    if(col == 'I' || col < 'A' || col > 'J') return false;
    x = col - 'A' - (col > 'I' ? 1 : 0);
    int row = atoi(v.c_str() + 1);
    if(row < 1 || row > 9) return false;
    y = 9 - row;
    return true;
}

// ---------- SGF logging ----------
static std::string sgfMoves;

static void sgfAdd(uint8_t color, int x, int y) {
    sgfMoves += ";";
    sgfMoves += (color == BLACK) ? "B[" : "W[";
    if(x >= 0) {
        sgfMoves += (char)('a' + x);
        sgfMoves += (char)('a' + y);
    }
    sgfMoves += "]";
}

static void sgfWrite(int gameNo, const std::string &result) {
    char path[256];
    snprintf(path, sizeof path, "game_%03d.sgf", gameNo);
    FILE *f = fopen(path, "w");
    fprintf(f, "(;GM[1]FF[4]SZ[9]KM[6.5]RE[%s]%s)\n",
            result.c_str(), sgfMoves.c_str());
    fclose(f);
}

static void printBoard(Game &g) {
    for(int y = 0; y < 9; y++) {
        for(int x = 0; x < 9; x++) {
            uint8_t c = g.at(x, y);
            printf("%c ", c == BLACK ? 'X' : c == WHITE ? 'O' : '.');
        }
        printf("\n");
    }
}

// ---------- search introspection ----------
// The whole AI is one translation unit here, so the tree statics are
// directly visible. Print the root's top children after a think().
static bool showStats = false;
static bool huntMode = false;
static double huntThresh = 6.0;
static FILE *huntLog = NULL;
static int huntCount = 0;

static void debugRootStats(int moveNo, uint8_t toMove) {
    struct Row { uint16_t v, w; uint8_t m; };
    Row top[3] = {};
    uint16_t total = 0;
    for(uint8_t c = node(0).firstChild; c != 0xFF; c = node(c).nextSibling) {
        uint16_t v = nVisits(c);
        if(v >= POISONED) continue;
        total += v;
        if(v > top[0].v) { top[2] = top[1]; top[1] = top[0];
                           top[0] = {v, nWins(c), node(c).move}; }
        else if(v > top[1].v) { top[2] = top[1];
                                top[1] = {v, nWins(c), node(c).move}; }
        else if(v > top[2].v) top[2] = {v, nWins(c), node(c).move};
    }
    printf("  mv%3d %s pool=%u root:", moveNo,
           toMove == BLACK ? "B" : "W", (unsigned)poolUsed);
    for(int i = 0; i < 3; i++) {
        if(!top[i].v) break;
        char mv[8];
        if(top[i].m == MOVE_PASS) snprintf(mv, sizeof mv, "pass");
        else snprintf(mv, sizeof mv, "%s",
                      toVertex(top[i].m % 9, top[i].m / 9).c_str());
        printf("  %s %u/%u=%.0f%%", mv, top[i].w, top[i].v,
               100.0 * top[i].w / top[i].v);
    }
    printf("\n");
}

// Parse "B+12.3"/"W+4.5" into a margin from `color`'s perspective
static double estMargin(const std::string &est, uint8_t color) {
    if(est.size() < 2 || (est[0] != 'B' && est[0] != 'W')) return 0;
    double v = atof(est.c_str() + 2);
    double black = est[0] == 'B' ? v : -v;
    return color == BLACK ? black : -black;
}

static void dumpTree(FILE *f, uint8_t chosen) {
    for(uint8_t c = node(0).firstChild; c != 0xFF; c = node(c).nextSibling) {
        uint16_t v = nVisits(c);
        if(v >= POISONED || v < 10) continue;
        uint8_t m = node(c).move;
        fprintf(f, "    %s%-5s v=%-4u q=%.0f%%\n",
                m == chosen ? ">" : " ",
                m == MOVE_PASS ? "pass" : toVertex(m % 9, m / 9).c_str(),
                v, 100.0 * nWins(c) / v);
    }
}

// ---------- game driver ----------
Game game;
AI ai;

// Raw playout win rate for `forColor` from the current game position,
// bypassing the tree entirely — measures the evaluator itself.
static double rawPlayoutWinrate(uint8_t forColor, int K) {
    simKomi = game.kpieces;
    rootTurn = game.turn;
    rngState = (uint16_t)rand() | 1;
    int wins = 0;
    for(int k = 0; k < K; k++) {
        for(uint8_t i = 0; i < BOARD_CELLS; i++)
            simBoard[i] = packedGet(game.board, i);
        cacheLibsPos = 0xFF;
        memset(raveMask, 0, sizeof(raveMask));
        if(playout(game.turn, NO_KO, 0xFF) == forColor) wins++;
    }
    return 100.0 * wins / K;
}


// Uniform-random legal opponent (skips filling its own single-point
// eyes so it can't immediately kill itself). Level -1 selects this.
static bool randomMove(uint8_t &x, uint8_t &y) {
    uint8_t cands[BOARD_CELLS];
    int n = 0;
    for(uint8_t i = 0; i < BOARD_CELLS; i++) {
        uint8_t cx = i % BOARD_SIZE, cy = i / BOARD_SIZE;
        if(game.at(cx, cy) != EMPTY) continue;
        bool ownEye = true;
        const int8_t dx[] = {-1, 1, 0, 0}, dy[] = {0, 0, -1, 1};
        for(int d = 0; d < 4; d++) {
            int nx2 = cx + dx[d], ny2 = cy + dy[d];
            if(nx2 < 0 || nx2 >= BOARD_SIZE || ny2 < 0 || ny2 >= BOARD_SIZE)
                continue;
            if(game.at(nx2, ny2) != game.turn) { ownEye = false; break; }
        }
        if(ownEye) continue;
        if(game.isValidMove(cx, cy)) cands[n++] = i;
    }
    if(!n) return false;
    uint8_t p = cands[rand() % n];
    x = p % BOARD_SIZE;
    y = p / BOARD_SIZE;
    return true;
}

// Area score on the final board (stones + one-sided empty regions):
// robust to the dead-stone litter a random opponent leaves behind.
static int areaMargin() {
    int black = 0, white = 0;
    for(uint8_t i = 0; i < BOARD_CELLS; i++) {
        uint8_t c = packedGet(game.board, i);
        if(c == BLACK) { black++; continue; }
        if(c == WHITE) { white++; continue; }
        uint8_t cx = i % BOARD_SIZE, cy = i / BOARD_SIZE;
        bool tb = false, tw = false;
        const int8_t dx[] = {-1, 1, 0, 0}, dy[] = {0, 0, -1, 1};
        for(int d = 0; d < 4; d++) {
            int nx2 = cx + dx[d], ny2 = cy + dy[d];
            if(nx2 < 0 || nx2 >= BOARD_SIZE || ny2 < 0 || ny2 >= BOARD_SIZE)
                continue;
            uint8_t nc = game.at(nx2, ny2);
            if(nc == BLACK) tb = true;
            if(nc == WHITE) tw = true;
        }
        if(tb && !tw) black++;
        else if(tw && !tb) white++;
    }
    return black * 2 - white * 2 - 13; // half-points, komi 6.5
}

// Play one game; aiColor is our AI's color. Returns +1 if our AI wins,
// -1 if gnugo wins, 0 on a rules disagreement (logged).
static int playGame(int gameNo, int level, uint8_t aiColor, bool verbose) {
    bool useGnugo = level >= 0;
    if(useGnugo) gtpStart(level, gameNo + 1);
    game.reset();
    ai.reset();
    sgfMoves.clear();

    std::string resp;
    int moves = 0;
    int rc = 0;
    bool gnugoResigned = false;
    bool aiResignedGame = false;

    while(!game.isGameOver() && moves < 250) {
        uint8_t color = game.turn;
        const char *cname = (color == BLACK) ? "black" : "white";

        if(color == aiColor) {
            uint8_t x = 0xFF, y = 0xFF;
            std::string preEst, gnugoSuggest;
            // Book move? chooseMove plays internally; diff to find it.
            uint8_t before[PACKED_BOARD_BYTES];
            memcpy(before, game.board, sizeof before);
            if(ai.chooseMove(game)) {
                for(uint8_t i = 0; i < BOARD_CELLS; i++)
                    if(packedGet(game.board, i) != EMPTY &&
                       packedGet(before, i) == EMPTY) {
                        x = i % BOARD_SIZE;
                        y = i / BOARD_SIZE;
                    }
            } else {
                if(huntMode && useGnugo) {
                    gtpCmd("estimate_score", preEst);
                    preEst = preEst.substr(0, preEst.find(' '));
                    gtpCmd(std::string("reg_genmove ") + cname, gnugoSuggest);
                }
                if(showStats && useGnugo) {
                    std::string est;
                    gtpCmd("estimate_score", est);
                    printf("  mv%3d gnugo=%-8s raw=%2.0f%%",
                           moves, est.substr(0, est.find(' ')).c_str(),
                           rawPlayoutWinrate(color, 200));
                }
                ai.think(game);
                if(ai.resigned) {
                    printf("  game %d: AI resigns at move %d (eval %u/%u = %d%%)\n",
                           gameNo, moves, thinkSimWins, thinkSims,
                           thinkSims ? (int)(100L * thinkSimWins / thinkSims) : 0);
                    aiResignedGame = true;
                    break;
                }
                if(showStats) debugRootStats(moves, color);
                if(ai.bestMove(game, x, y)) {
                    game.playMove(x, y);
                    ai.notifyMove(x, y);
                } else {
                    x = 0xFF;
                    game.pass();
                    ai.notifyPass();
                }
            }
            if(x != 0xFF) {
                sgfAdd(color, x, y);
                if(huntMode && useGnugo && !preEst.empty()) {
                    // play, then ask gnugo what our move did to us
                    if(gtpCmd(std::string("play ") + cname + " " +
                              toVertex(x, y), resp)) {
                        std::string postEst;
                        gtpCmd("estimate_score", postEst);
                        postEst = postEst.substr(0, postEst.find(' '));
                        double drop = estMargin(preEst, color) -
                                      estMargin(postEst, color);
                        if(drop >= huntThresh) {
                            huntCount++;
                            fprintf(huntLog,
                                "== blunder %d: game %d mv %d %s played %s"
                                " (drop %.1f: %s -> %s, gnugo wanted %s)\n",
                                huntCount, gameNo, moves, cname,
                                toVertex(x, y).c_str(), drop,
                                preEst.c_str(), postEst.c_str(),
                                gnugoSuggest.c_str());
                            for(int by = 0; by < 9; by++) {
                                fprintf(huntLog, "    ");
                                for(int bx = 0; bx < 9; bx++) {
                                    uint8_t cc = game.at(bx, by);
                                    fputc(cc == BLACK ? 'X' :
                                          cc == WHITE ? 'O' : '.', huntLog);
                                }
                                fputc('\n', huntLog);
                            }
                            dumpTree(huntLog, y * 9 + x);
                            char fn[64];
                            snprintf(fn, sizeof fn, "hunt_%03d.sgf",
                                     huntCount);
                            FILE *hf = fopen(fn, "w");
                            fprintf(hf, "(;GM[1]FF[4]SZ[9]KM[6.5]%s)\n",
                                    sgfMoves.c_str());
                            fclose(hf);
                            fflush(huntLog);
                        }
                    } else {
                        printf("game %d: gnugo REJECTED our %s %s\n",
                               gameNo, cname, toVertex(x, y).c_str());
                        rc = -2;
                        break;
                    }
                } else if(useGnugo &&
                   !gtpCmd(std::string("play ") + cname + " " + toVertex(x, y), resp)) {
                    printf("game %d: gnugo REJECTED our %s %s (%s)\n",
                           gameNo, cname, toVertex(x, y).c_str(), resp.c_str());
                    rc = -2;
                    break;
                }
            } else {
                sgfAdd(color, -1, -1);
                if(useGnugo)
                    gtpCmd(std::string("play ") + cname + " pass", resp);
            }
        } else if(!useGnugo) {
            uint8_t x, y;
            if(randomMove(x, y)) {
                game.playMove(x, y);
                sgfAdd(color, x, y);
                ai.notifyMove(x, y);
            } else {
                sgfAdd(color, -1, -1);
                game.pass();
                ai.notifyPass();
            }
        } else {
            gtpCmd(std::string("genmove ") + cname, resp);
            if(resp == "resign") {
                gnugoResigned = true;
                break;
            }
            if(resp == "PASS" || resp == "pass") {
                sgfAdd(color, -1, -1);
                game.pass();
                ai.notifyPass();
            } else {
                uint8_t x, y;
                if(!fromVertex(resp, x, y)) {
                    printf("game %d: unparseable gnugo move '%s'\n",
                           gameNo, resp.c_str());
                    rc = -2;
                    break;
                }
                if(!game.playMove(x, y)) {
                    printf("game %d: our rules REJECT gnugo's %s %s\n",
                           gameNo, cname, resp.c_str());
                    rc = -2;
                    break;
                }
                sgfAdd(color, x, y);
                ai.notifyMove(x, y);
            }
        }
        moves++;
    }

    std::string result;
    if(rc == -2) {
        result = "?";
    } else if(gnugoResigned) {
        result = (aiColor == BLACK) ? "B+R" : "W+R";
        rc = 1;
    } else if(aiResignedGame) {
        result = (aiColor == BLACK) ? "W+R" : "B+R";
        rc = -1;
    } else if(!useGnugo) {
        int m = areaMargin(); // half-points, B minus W
        char buf[32];
        snprintf(buf, sizeof buf, "%c+%.1f", m > 0 ? 'B' : 'W',
                 (m > 0 ? m : -m) / 2.0);
        result = buf;
        rc = ((m > 0) == (aiColor == BLACK)) ? 1 : -1;
    } else {
        gtpCmd("final_score", result);
        uint8_t winner = (result[0] == 'B') ? BLACK : WHITE;
        rc = (winner == aiColor) ? 1 : -1;
    }

    sgfWrite(gameNo, result);
    printf("game %03d: level %d, AI=%s, %3d moves, %s  %s\n",
           gameNo, level, aiColor == BLACK ? "B" : "W", moves,
           result.c_str(), rc > 0 ? "AI WIN" : rc < 0 ? "ai loss" : "");
    if(verbose) printBoard(game);
    fflush(stdout);
    if(useGnugo) gtpStop();
    return rc;
}

// Eval benchmark: replay an SGF, and at each sampled position print
// gnugo's score estimate beside our raw playout win rate (for Black).
// Measures the evaluator itself with no game-play noise.
static int evalBench(const char *path) {
    FILE *f = fopen(path, "r");
    if(!f) { printf("cannot open %s\n", path); return 1; }
    char buf[8192];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    buf[n] = 0;
    fclose(f);

    std::vector<std::pair<uint8_t, int>> movesList; // color, pos (-1 pass)
    for(char *p = buf; (p = strchr(p, ';')); p++) {
        uint8_t color;
        if(p[1] == 'B') color = BLACK;
        else if(p[1] == 'W') color = WHITE;
        else continue;
        if(p[2] != '[') continue;
        if(p[3] == ']') movesList.push_back({color, -1});
        else movesList.push_back({color, (p[4] - 'a') * 9 + (p[3] - 'a')});
    }

    gtpStart(10, 1); // level irrelevant: only estimate_score is used
    game.reset();
    std::string resp;
    printf("%-6s %-10s %s\n", "move", "gnugo", "rawB%");
    for(size_t k = 0; k < movesList.size() && k < 70; k++) {
        auto [color, pos] = movesList[k];
        const char *cname = (color == BLACK) ? "black" : "white";
        if(pos < 0) {
            game.pass();
            gtpCmd(std::string("play ") + cname + " pass", resp);
        } else {
            if(!game.playMove(pos % 9, pos / 9)) {
                printf("replay diverged at move %zu\n", k);
                break;
            }
            gtpCmd(std::string("play ") + cname + " " +
                   toVertex(pos % 9, pos / 9), resp);
        }
        std::string est;
        gtpCmd("estimate_score", est);
        double raw = (k & 3) == 0 ? rawPlayoutWinrate(BLACK, 300) : -1;
        printf("%-4zu %s %-4s  %-10s", k, cname[0] == 'b' ? "B" : "W",
               pos < 0 ? "pass" : toVertex(pos % 9, pos / 9).c_str(),
               est.substr(0, est.find(' ')).c_str());
        if(raw >= 0) printf(" rawB=%2.0f%%", raw);
        printf("\n");
    }
    gtpStop();
    return 0;
}

int main(int argc, char **argv) {
    if(argc > 2 && std::string(argv[1]) == "hunt") {
        huntMode = true;
        int games = atoi(argv[2]);
        int level = argc > 3 ? atoi(argv[3]) : 0;
        if(argc > 4) huntThresh = atof(argv[4]);
        int off = argc > 5 ? atoi(argv[5]) : 0; // fresh games per cycle
        huntLog = fopen("hunt_report.txt", "w");
        srand(4242 + off);
        int w = 0, l = 0;
        for(int g = 0; g < games; g++) {
            int r = playGame(g + off, level, (g & 1) ? WHITE : BLACK, false);
            if(r > 0) w++; else l++;
        }
        fprintf(huntLog, "== done: %d games, %d blunders logged\n",
                games, huntCount);
        fclose(huntLog);
        printf("hunt done: %d games (%d-%d), %d blunders -> hunt_report.txt\n",
               games, w, l, huntCount);
        return 0;
    }
    if(argc > 2 && std::string(argv[1]) == "eval") {
        srand(12345);
        if(argc > 3) mctsIterations = atoi(argv[3]);
        return evalBench(argv[2]);
    }
    int games = argc > 1 ? atoi(argv[1]) : 10;
    int level = argc > 2 ? atoi(argv[2]) : 1;
    showStats = argc > 3 && atoi(argv[3]) != 0;
    if(argc > 4) mctsIterations = atoi(argv[4]);
    if(argc > 5) reclaimEnabled = atoi(argv[5]);
    if(argc > 6) resignStreak = atoi(argv[6]);
    int seedOffset = argc > 7 ? atoi(argv[7]) : 0;
    srand(12345 + seedOffset);
    fprintf(stderr, "iterations=%u\n", (unsigned)mctsIterations);

    int wins = 0, losses = 0, errs = 0;
    for(int g = 0; g < games; g++) {
        int r = playGame(g + seedOffset, level, (g & 1) ? WHITE : BLACK, false);
        if(r > 0) wins++;
        else if(r == -1) losses++;
        else errs++;
    }
    printf("\n=== vs gnugo level %d: %d wins, %d losses, %d errors (%d games) ===\n",
           level, wins, losses, errs, games);
    return 0;
}
