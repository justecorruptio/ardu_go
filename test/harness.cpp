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
#ifdef PONDER_SIM
static uint16_t psPonderIters;   // PONDER_SIM env: iters per opponent turn
static uint8_t psHalve = 1;      // PONDER_HALVE env: adoption discount (TREUSE precedent)
#endif

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
        // KATAGO_HUMAN: use the KataGo human-SL net as the opponent (rank via
        // KATAGO_RANK, default preaz_10k). 1 visit + temperature = truest rank
        // match. Chinese/area rules + komi 6.5 to match everything else.
        if(getenv("KATAGO_HUMAN")) {
            const char *rank = getenv("KATAGO_RANK");
            if(!rank) rank = "preaz_10k";
            char ovr[640];
            snprintf(ovr, sizeof ovr,
                "maxVisits=1,humanSLProfile=%s,humanSLChosenMoveProp=1.0,"
                "humanSLCpuctExploration=0.50,chosenMoveTemperature=0.70,"
                "chosenMoveTemperatureEarly=0.85,chosenMoveTemperatureHalflife=80,"
                "chosenMoveTemperatureOnlyBelowProb=0.01,rules=chinese,"
                "allowResignation=false,numSearchThreads=1,searchRandSeed=%d", rank, seed);
            execlp("katago", "katago", "gtp",
                   "-model", "/tmp/kata9x9.bin.gz",
                   "-human-model", "/tmp/kata_human.bin.gz",
                   "-config", "/Users/jay/.katago/gtp.cfg",
                   "-override-config", ovr, (char *)NULL);
            _exit(127);
        }
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
// Phase-leak histogram: exchange balance, parity-clean. gnugo's
// estimate swings with the side to move, so single-move deltas are
// contaminated (~-10/move offset); instead compare the PRE-move
// estimate at our turn k to the one at our previous turn — one full
// exchange (our move + their reply). Positive = we lost ground.
// phase 0 = opening (mv<=15), 1 = middle (<=35), 2 = endgame;
// classes: <0 (gained), 0-1, 1-3, 3-6, >6.
static long phaseN[3][5];
static double exchSum[3];
static long exchMoves[3];
static double lastPreMargin;
static int lastPreValid = 0, lastPreGame = -1;
static FILE *openDiagLog = NULL;

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
    // sorted by visits: sibling order is push-front (junk-last-added
    // first) and once hid the chosen move below a truncated view
    struct Row { uint16_t v, w; uint8_t m; };
    Row rows[24];
    int n = 0;
    for(uint8_t c = node(0).firstChild; c != 0xFF; c = node(c).nextSibling) {
        uint16_t v = nVisits(c);
        if(v >= POISONED || v < 8) continue;
        Row r = {v, nWins(c), node(c).move};
        int i = n < 24 ? n : 23;
        while(i > 0 && rows[i - 1].v < r.v) {
            if(i < 24) rows[i] = rows[i - 1];
            i--;
        }
        if(i < 24) rows[i] = r;
        if(n < 24) n++;
    }
    for(int i = 0; i < n && i < 16; i++)
        fprintf(f, "    %s%-5s v=%-4u q=%.0f%%\n",
                rows[i].m == chosen ? ">" : " ",
                rows[i].m == MOVE_PASS ? "pass"
                    : toVertex(rows[i].m % 9, rows[i].m / 9).c_str(),
                rows[i].v, 100.0 * rows[i].w / rows[i].v);
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
// Targeted-validation seeding (peepseed mode): when seedSeq is set,
// playGame pre-plays it (alternating colours from Black) into
// game+ai+gnugo, and records the engine's FIRST reply in
// firstEngineMove (0xFE = pass) so the driver can check it.
static const char *seedSeq = nullptr;
static uint8_t firstEngineMove = 0xFF;
static bool recordFirst = false;

// -1 if gnugo wins, 0 on a rules disagreement (logged).
static int playGame(int gameNo, int level, uint8_t aiColor, bool verbose) {
    bool useGnugo = level >= 0;
    if(useGnugo) gtpStart(level, gameNo + 1);
    if(useGnugo && getenv("KATAGO_HUMAN")) {   // KataGo needs board/komi via GTP
        std::string r; gtpCmd("boardsize 9", r); gtpCmd("komi 6.5", r);
    }
    game.reset();
    ai.reset();
#ifdef PONDER_SIM
    psWarmValid = psTreeFresh = 0;   // never carry a tree across games
#endif
    // Per-game engine RNG seed (2026-08): previously rngState free-ran
    // across a worker's whole batch, so any behavioral divergence in
    // game k desynced every later game -- "paired" gauntlets were
    // silently unpaired past the first divergence, and batch games
    // could not be reproduced standalone. Knuth-hash the game number.
    rngState = (uint16_t)(2654435761u * (uint32_t)(gameNo + 1) >> 16) | 1;
#ifdef THINK_TRACE
    fprintf(stderr, "GAME %d seed=%u\n", gameNo, rngState);
#endif
    sgfMoves.clear();

    std::string resp;
    if(seedSeq) {
        const char *p = seedSeq;
        uint8_t color = BLACK;
        while(*p) {
            while(*p == ' ') p++;
            const char *e = p;
            while(*e && *e != ' ') e++;
            std::string v(p, e - p);
            uint8_t sx, sy;
            fromVertex(v, sx, sy);
            game.playMove(sx, sy);
            ai.notifyMove(sx, sy);
            sgfAdd(color, sx, sy);
            if(useGnugo)
                gtpCmd(std::string("play ") +
                       (color == BLACK ? "black" : "white") + " " + v, resp);
            color = 3 - color;
            p = e;
        }
        firstEngineMove = 0xFF;
        recordFirst = true;
    }
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
                if(huntMode && useGnugo && huntThresh <= 100) {
                    // thresh > 100 = pure paired-gauntlet mode: skip the
                    // per-move estimate/suggest GTP round trips (~2x faster;
                    // read-only queries, game results identical)
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
                // Device honesty: on hardware every rendered frame
                // overwrites the sBuffer-hosted pool nodes between
                // thinks (only poolExt survives). Scribble the same
                // region here so host games cannot depend on -- or
                // measure benefits from -- state a real device loses.
#ifdef PONDER_SIM
                // v2 ponder models the blit design: rendering never touches
                // sBuffer, so the pool survives between thinks. Scribble only
                // when no warm tree is armed (cold path keeps device honesty).
                if(!psWarmValid)
#endif
                {
                memset(pool, 0xA5, sizeof(Node) * NODE_POOL_SB);
                memset(pool, 0xA5, sizeof(Node) * NODE_POOL_SB); // device honesty (see playGame)
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
#ifdef PONDER_SIM
                // v2: re-root the just-searched tree at OUR move (the
                // opponent-to-move position is bm's subtree), then ponder on
                // it for the budget the "human's thinking time" affords. The
                // tree persists in the pool for adoption at their reply.
                if(psPonderIters && !ai.resigned) {
                    psReRootTo(x != 0xFF ? (uint8_t)(y * 9 + x) : MOVE_PASS);
                    ai.ponderThink(game, psPonderIters);
                }
#endif
            }
            if(recordFirst) {
                firstEngineMove = (x == 0xFF) ? 0xFE : (uint8_t)(y * 9 + x);
                recordFirst = false;
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
                        // False-positive filter: if we played the very
                        // move gnugo recommends, any estimate swing is
                        // evaluator noise, not a blunder (seen live:
                        // a "22.5-point drop" on gnugo's own choice).
                        if(gnugoSuggest == toVertex(x, y)) drop = 0;
                        {
                            double m = estMargin(preEst, color);
                            if(gameNo != lastPreGame) {
                                lastPreGame = gameNo;
                                lastPreValid = 0;
                            }
                            if(lastPreValid) {
                                double exch = lastPreMargin - m;
                                int ph = moves <= 15 ? 0 :
                                         moves <= 35 ? 1 : 2;
                                int sc = exch < 0 ? 0 : exch < 1 ? 1 :
                                         exch < 3 ? 2 : exch < 6 ? 3 : 4;
                                phaseN[ph][sc]++;
                                exchSum[ph] += exch;
                                exchMoves[ph]++;
                            }
                            lastPreMargin = m;
                            lastPreValid = 1;
                        }
                        if(openDiagLog && moves <= 200)
                            fprintf(openDiagLog,
                                "game %d mv %2d %s AI=%-3s gnugo=%-3s drop=%5.1f  %s->%s\n",
                                gameNo, moves, cname, toVertex(x, y).c_str(),
                                gnugoSuggest.c_str(), drop, preEst.c_str(),
                                postEst.c_str());
                        if(drop >= huntThresh && huntLog) {
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
#ifdef PONDER_SIM
                if(psPonderIters && psReRootTo(MOVE_PASS)) {
                    if(psHalve) psHalveTree(); psHits++;
                    psCarryVisits += nRefVisits(node(0));
                }
#endif
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
#ifdef PONDER_SIM
                if(psPonderIters && psReRootTo((uint8_t)(y * 9 + x))) {
                    if(psHalve) psHalveTree(); psHits++;   // TREUSE staleness precedent
                    psCarryVisits += nRefVisits(node(0));
                }
#endif
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

static void psReport(void) {
#ifdef PONDER_SIM
    if(psPonders || thinkItersRun)
        fprintf(stderr, "PONDER ponders=%lu adopt=%lu (%.0f%%) carryV=%lu "
                "(mean %.0f) ownIters=%lu ownBudget=%lu ponderSpent=%lu\n",
                (unsigned long)psPonders, (unsigned long)psHits,
                psPonders ? 100.0 * psHits / psPonders : 0.0,
                (unsigned long)psCarryVisits,
                psHits ? (double)psCarryVisits / psHits : 0.0,
                (unsigned long)thinkItersRun, (unsigned long)thinkItersBudget,
                (unsigned long)psPonderSpent);
#endif
}
int main(int argc, char **argv) {
#ifdef PONDER_SIM
    if(getenv("PONDER_SIM")) psPonderIters = (uint16_t)atoi(getenv("PONDER_SIM"));
    if(getenv("PONDER_HALVE")) psHalve = (uint8_t)atoi(getenv("PONDER_HALVE"));
    atexit(psReport);
#endif
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
            // colour is a function of the GAME NUMBER (not the loop
            // index) so any game reproduces identically in any batch
            int r = playGame(g + off, level,
                             ((g + off) & 1) ? WHITE : BLACK, false);
            if(r > 0) w++; else l++;
            printf("RESULT %d %d\n", g + off, r > 0 ? 1 : 0);  // per-game for paired McNemar
        }
        fprintf(huntLog, "== done: %d games, %d blunders logged\n",
                games, huntCount);
        for(int ph = 0; ph < 3; ph++)
            fprintf(huntLog,
                "== phase %s: exch %ld avgloss %+.2f  gained:%ld 0-1:%ld"
                " 1-3:%ld 3-6:%ld >6:%ld\n",
                ph == 0 ? "open" : ph == 1 ? "mid " : "end ",
                exchMoves[ph], exchMoves[ph] ? exchSum[ph] / exchMoves[ph] : 0,
                phaseN[ph][0], phaseN[ph][1], phaseN[ph][2], phaseN[ph][3],
                phaseN[ph][4]);
        fclose(huntLog);
        printf("hunt done: %d games (%d-%d), %d blunders -> hunt_report.txt\n",
               games, w, l, huntCount);
        printf("ITERS %u %u\n", (unsigned)thinkItersRun,
               (unsigned)thinkItersBudget);
        return 0;
    }
    // ldcheck: stage-1 L&D classifier accuracy vs the ownership vote.
    // Reads SGF paths on stdin, replays each, samples positions every
    // 8 moves from move 20, tags stones via ldClassify, then scores
    // the same position with ownVote (64 scoring playouts) as ground
    // truth. Reports a confusion matrix + CRITICAL trigger rate.
#ifdef LD_CLASS
    if(argc > 1 && std::string(argv[1]) == "ldcheck") {
        char path[512];
        long conf[4][3] = {{0}};   // [tag][voteAlive, voteDead, undecided]
        long posN = 0, posCrit = 0, gamesN = 0;
        while(fgets(path, sizeof(path), stdin)) {
            std::string p(path);
            while(!p.empty() && (p.back() == '\n' || p.back() == ' ')) p.pop_back();
            if(p.empty()) continue;
            FILE *f = fopen(p.c_str(), "r");
            if(!f) continue;
            std::string sgf;
            char buf[4096]; size_t r;
            while((r = fread(buf, 1, sizeof(buf), f)) > 0) sgf.append(buf, r);
            fclose(f);
            // collect moves
            std::vector<std::pair<uint8_t,uint8_t>> mv;
            for(size_t i = 0; i + 5 < sgf.size(); i++)
                if(sgf[i] == ';' && (sgf[i+1] == 'B' || sgf[i+1] == 'W') &&
                   sgf[i+2] == '[') {
                    if(sgf[i+3] == ']') { mv.push_back({0xFF, 0xFF}); continue; }
                    mv.push_back({(uint8_t)(sgf[i+3]-'a'), (uint8_t)(sgf[i+4]-'a')});
                }
            if(mv.size() < 24) continue;
            gamesN++;
            game.reset();
            for(size_t m = 0; m < mv.size(); m++) {
                if(mv[m].first == 0xFF) game.pass();
                else game.playMove(mv[m].first, mv[m].second);
                if(m + 1 < 20 || (m + 1 - 20) % 8 || m + 8 >= mv.size())
                    continue;
                // sample this position
                for(uint8_t i = 0; i < BOARD_CELLS; i++)
                    simBoard[i] = packedGet(game.board, i);
                ldClassify();
                uint8_t snap[BOARD_CELLS];
                uint8_t anyCrit = 0;
                for(uint8_t i = 0; i < BOARD_CELLS; i++) {
                    snap[i] = ldCellStatus(i);
                    if(snap[i] == LD_CRIT) anyCrit = 1;
                }
                posN++; posCrit += anyCrit;
                uint8_t own[BOARD_CELLS];
                ownVote(game, own);
                for(uint8_t i = 0; i < BOARD_CELLS; i++) {
                    uint8_t st = packedGet(game.board, i);
                    if(st == EMPTY) continue;
                    uint8_t o = own[i]; // black-owned count of SCORE_PLAYOUTS
                    uint8_t aliveT = (st == BLACK) ? (o >= 48) : (o <= 16);
                    uint8_t deadT  = (st == BLACK) ? (o <= 16) : (o >= 48);
                    conf[snap[i]][aliveT ? 0 : deadT ? 1 : 2]++;
                }
            }
        }
        const char *nm[4] = {"none", "ALIVE", "DEAD", "CRIT"};
        printf("games %ld  positions %ld  critical-rate %.1f%%\n",
               gamesN, posN, posN ? 100.0 * posCrit / posN : 0);
        printf("%-6s %10s %10s %10s   precision(decided)\n",
               "tag", "voteAlive", "voteDead", "undecided");
        for(int t = 0; t < 4; t++) {
            long a = conf[t][0], d = conf[t][1], u = conf[t][2];
            long dec = a + d;
            double prec = dec ? (t == 2 ? 100.0 * d / dec : 100.0 * a / dec) : 0;
            printf("%-6s %10ld %10ld %10ld   %.1f%%\n", nm[t], a, d, u, prec);
        }
        return 0;
    }
#endif
    // priorprobe: print the D5 prior in the seed-0 peeped-jump
    // position — a build FINGERPRINT so gauntlet arms can prove which
    // prior code they contain before burning 1000 games (stale arm
    // binaries have now voided two runs).
    if(argc > 1 && std::string(argv[1]) == "priorprobe") {
        game.reset(); ai.reset();
        const char *seq[] = {"D4", "F3", "D6", "E5"};
        for(int i = 0; i < 4; i++) {
            uint8_t sx, sy;
            fromVertex(seq[i], sx, sy);
            game.playMove(sx, sy);
            ai.notifyMove(sx, sy);
        }
        rootTurn = game.turn;
        for(uint8_t i = 0; i < BOARD_CELLS; i++)
            simBoard[i] = packedGet(game.board, i);
        rootStones = 4;
        buildChainMap();
#ifdef CFG_PRIOR
        buildCfgDist(4 * 9 + 4);   // last = E5, engine path runs this in widenNode
        {
            uint8_t r1 = 0, r2 = 0, r3 = 0;
            for(uint8_t i = 0; i < BOARD_CELLS; i++) {
                if(cfgDist[i] == 1) r1++;
                else if(cfgDist[i] == 2) r2++;
                else if(cfgDist[i] == 3) r3++;
            }
            printf("CFGRINGS %u/%u/%u ", r1, r2, r3);
        }
#endif
        // Prior FIRST (the playout hash below trashes simBoard/chainId)
        int8_t pr = candidatePrior(4 * 9 + 3, game.turn, 4 * 9 + 4, 0);
        // Playout-policy hash: 200 fixed-seed playouts from this
        // position — distinguishes arms whose difference lives in the
        // playout policy, which the prior value cannot see.
        int pw = 0;
        simKomi = game.kpieces;
        rootTurn = game.turn;
        for(int k = 0; k < 200; k++) {
            rngState = (uint16_t)(k * 2654435761u >> 16) | 1;
            for(uint8_t i = 0; i < BOARD_CELLS; i++)
                simBoard[i] = packedGet(game.board, i);
            pw += playout(game.turn, 0xFF, 4 * 9 + 4) == game.turn;
        }
        // Tree fingerprint: one deterministic think from the seed
        // position (fixed rng) — catches tree/search-side changes the
        // playout hash cannot see.
        rngState = 31337;
        ai.think(game);
        uint8_t tx = 0xFF, ty = 0xFF;
        uint8_t tmv = ai.bestMove(game, tx, ty) ? (uint8_t)(ty * 9 + tx) : 81;
#ifdef UCB_SHADOW
        { extern uint32_t shadowSel, shadowMismatch; extern double shadowGapSum;
          printf("UCBSHADOW selections=%u mismatches=%u (%.4f%%) meanGapQ=%.6f\n",
                 (unsigned)shadowSel, (unsigned)shadowMismatch,
                 shadowSel ? 100.0*shadowMismatch/shadowSel : 0.0,
                 shadowMismatch ? shadowGapSum/shadowMismatch : 0.0); }
#endif
        printf("FINGERPRINT prior(D5)=%+d iters=%u pw=%d/200 think=%u/%u\n",
               pr, (unsigned)mctsIterations, pw, tmv, thinkSims);
        return 0;
    }

    // peepseed <seedIdx 0-7> [trials] [level] [off]: replay a curated
    // opening that ends with an opponent peep against our one-point
    // jump, record whether the engine's first move blocks the
    // connector, then play the game out vs gnugo and score it.
    if(argc > 2 && std::string(argv[1]) == "peepseed") {
        struct Seed { const char *seq; const char *conn; uint8_t aiColor; };
        static const Seed SEEDS[] = {
            {"D4 F3 D6 E5", "D5", BLACK},   // vertical jump, peep from E
            {"D4 F3 D6 C5", "D5", BLACK},   // peep from W
            {"F4 C3 F6 E5", "F5", BLACK},   // right-side jump
            {"C6 G3 E6 D5", "D6", BLACK},   // horizontal jump, peep below
            {"G7 D4 C3 D6 E5", "D5", WHITE},
            {"C7 F4 G3 F6 E5", "F5", WHITE},
            {"G7 E3 C7 E5 F4", "E4", WHITE},
            {"D3 E4 F7 E6 D5", "E5", WHITE},
        };
        int si = atoi(argv[2]);
        int trials = argc > 3 ? atoi(argv[3]) : 25;
        int level = argc > 4 ? atoi(argv[4]) : 0;
        int off = argc > 5 ? atoi(argv[5]) : 0;
        const Seed &S = SEEDS[si];
        seedSeq = S.seq;
        uint8_t cx, cy;
        fromVertex(S.conn, cx, cy);
        uint8_t conn = cy * 9 + cx;
        int wins = 0, answered = 0;
        for(int r = 0; r < trials; r++) {
            rngState = (uint16_t)(si * 7919 + r * 131 + 17) | 1;
            int rc = playGame(off + si * 1000 + r, level, S.aiColor, false);
            bool ans = firstEngineMove == conn;
            answered += ans;
            if(rc > 0) wins++;
            printf("PTRIAL %d %d ans=%d win=%d\n", si, r, (int)ans,
                   rc > 0 ? 1 : 0);
        }
        printf("SEED %d: answered %d/%d, wins %d/%d\n",
               si, answered, trials, wins, trials);
        return 0;
    }

    if(argc > 2 && std::string(argv[1]) == "opendiag") {
        huntMode = true;                 // reuse the reg_genmove + estimate path
        int games = atoi(argv[2]);
        int off = argc > 3 ? atoi(argv[3]) : 0;
        if(argc > 4) mctsIterations = atoi(argv[4]);
        openDiagLog = fopen("opendiag.txt", "w");
        setvbuf(openDiagLog, NULL, _IOLBF, 0);   // line-buffered: survive a crash
        srand(4242 + off);
        for(int g = 0; g < games; g++) {
            fprintf(stderr, "  [game %d]\n", g + off);
            playGame(g + off, 0, (g & 1) ? WHITE : BLACK, false);
        }
        fclose(openDiagLog);
        printf("opendiag done: %d games -> opendiag.txt\n", games);
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
        int r = playGame(g + seedOffset, level,
                         ((g + seedOffset) & 1) ? WHITE : BLACK, false);
        if(r > 0) wins++;
        else if(r == -1) losses++;
        else errs++;
    }
    printf("\n=== vs gnugo level %d: %d wins, %d losses, %d errors (%d games) ===\n",
           level, wins, losses, errs, games);
    return 0;
}
