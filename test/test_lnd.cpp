// Life & death regression test: canonical small-eyespace positions
// where one move decides life. Passes when the AI finds the vital
// point from both sides.
#include <stdio.h>
#include <string.h>

#include "../game.cpp"
#include "../ai.cpp"

uint8_t Arduboy2Base::sBuffer[1024];

static Game game;
static AI ai;

// Board setup from a 9-line picture: X black, O white, . empty
static void setup(const char *rows[9], uint8_t toMove) {
    game.reset();
    ai.reset();
    for(uint8_t y = 0; y < 9; y++)
        for(uint8_t x = 0; x < 9; x++) {
            char c = rows[y][x];
            game.set(x, y, c == 'X' ? BLACK : c == 'O' ? WHITE : EMPTY);
        }
    memcpy(game.prevBoard, game.board, sizeof(game.board));
    game.turn = toMove;
    // Low komi widens the reward for correct life-and-death play well
    // past playout noise: with 6.5 the kill won by only 2.5, which
    // read WORSE than a fantasy invasion of the big open territory.
    game.kpieces = 1;
}

// One search trial; returns 1 on the expected move. Reseeds so each
// trial samples a different playout stream.
static int trial(uint8_t ex, uint8_t ey, uint8_t exX2, uint8_t exY2,
                 uint8_t *gx, uint8_t *gy) {
    ai.reset();
    ai.think(game);
    uint8_t x = 0xFF, y = 0xFF;
    uint8_t got = ai.bestMove(game, x, y);
    *gx = x; *gy = y;
    if(!got) return 0;
    return (x == ex && y == ey) || (x == exX2 && y == exY2);
}

// Majority of 3 differently-seeded searches: single-seed results flip
// on any code change (each prior value shifts the rng stream), which
// once misled a whole tuning session. "Usually right" is the honest
// assertion for a stochastic searcher.
static int expectMove(const char *name, uint8_t ex, uint8_t ey) {
    uint8_t x = 0xFF, y = 0xFF;
    int wins = 0;
    for(int t = 0; t < 3; t++) {
        srand(1000 + t * 7777);
        wins += trial(ex, ey, ex, ey, &x, &y);
    }
    bool ok = wins >= 2;
    uint8_t got = 1;
    printf("%-28s expected (%d,%d) %d/3 last(%d,%d)  %s\n",
           name, ex, ey, wins, x, y, ok ? "PASS" : "FAIL");
    // Search introspection: overall eval, the expected move's stats,
    // and the visit leader
    uint8_t expPos = ey * BOARD_SIZE + ex;
    uint16_t leadV = 0, leadW = 0;
    uint8_t leadM = 0xFF;
    uint16_t expV = 0, expW = 0;
    for(uint8_t c = node(0).firstChild; c != 0xFF; c = node(c).nextSibling) {
        uint16_t v = nVisits(c);
        if(v >= POISONED) continue;
        if(node(c).move == expPos) { expV = v; expW = nWins(c); }
        if(v > leadV) { leadV = v; leadW = nWins(c); leadM = node(c).move; }
    }
    printf("    eval %u/%u  expected-move %u/%u  leader (%d,%d) %u/%u\n",
           thinkSimWins, thinkSims, expW, expV,
           leadM % 9, leadM / 9, leadW, leadV);
    return ok ? 0 : 1;
}

int main() {
    srand(12345);
    int fails = 0;

    // Settled full-board positions where the bottom-left white
    // group's life decides the game: black wall col 4, white wall
    // col 5, white territory right. White alive -> white wins by
    // ~10; white dead -> black wins by ~4. The vital point is the
    // only move that matters for either side.

    // Straight-three eyespace at a1,b1,c1; vital = b1 = (1,8)
    const char *straight3[9] = {
        "....XO...",
        "....XO...",
        "....XO...",
        "....XO...",
        "....XO...",
        "....XO...",
        "XXXXXO...",
        "OOOOXO...",
        "...OXO...",
    };
    setup(straight3, BLACK);
    fails += expectMove("straight-3: black kills", 1, 8);
    setup(straight3, WHITE);
    fails += expectMove("straight-3: white lives", 1, 8);

    // Bent-three eyespace at a1,b1,a2; vital = the bend = a1 = (0,8)
    const char *bent3[9] = {
        "....XO...",
        "....XO...",
        "....XO...",
        "....XO...",
        "....XO...",
        "XXX.XO...",
        "OOOXXO...",
        ".OOXXO...",
        "..OXXO...",
    };
    setup(bent3, BLACK);
    fails += expectMove("bent-3: black kills", 0, 8);
    setup(bent3, WHITE);
    fails += expectMove("bent-3: white lives", 0, 8);

    // Boundary push: both sides hold a wall, Black just pushed into
    // the neutral line in contact with White's wall (E7 area). White
    // must answer near the push — the reported failure was tenuki
    // while the opponent kept walking in.
    const char *push[9] = {
        "...X.O...",
        "...X.O...",
        "...X.O...",
        "...XXO...",
        "...X.O...",
        "...X.O...",
        "..XX.OO..",
        "...X.O...",
        "...X.O...",
    };
    setup(push, WHITE);
    // Black's newest stone is E6 = (4,3): remove it from prevBoard so
    // the AI sees it as the last move (locality + block priors key
    // off it).
    packedSet(game.prevBoard, 3 * 9 + 4, EMPTY);
    {
        uint8_t x, y;
        int wins = 0;
        for(int t = 0; t < 3; t++) {
            srand(1000 + t * 7777);
            ai.reset();
            ai.think(game);
            x = y = 0xFF;
            if(ai.bestMove(game, x, y)) {
                int ddx = x > 4 ? x - 4 : 4 - x;
                int ddy = y > 3 ? y - 3 : 3 - y;
                wins += (ddx <= 1 && ddy <= 1);
            }
        }
        bool ok = wins >= 2;
        printf("boundary push: answers locally  %d/3 last(%d,%d)  %s\n",
               wins, x, y, ok ? "PASS" : "FAIL");
        if(!ok) fails++;
    }

    // Cut protection: White's wall has a single connecting point at
    // (5,4); Black's newest stone (4,4) stares straight at it. White
    // must connect — the reported failure was groups getting
    // disconnected because cut points were only defended after a
    // chain was already at 2 liberties.
    const char *cut[9] = {
        "...X.O...",
        "...X.O...",
        "...X.O.O.",
        "...X.O.O.",
        "...XX....",
        "...X.O.O.",
        "...X.O.O.",
        "...X.O...",
        "...X.O...",
    };
    setup(cut, WHITE);
    packedSet(game.prevBoard, 4 * 9 + 4, EMPTY); // (4,4) is the last move
    {
        uint8_t x, y;
        int wins = 0;
        for(int t = 0; t < 3; t++) {
            srand(1000 + t * 7777);
            wins += trial(5, 4, 5, 4, &x, &y);
        }
        bool ok = wins >= 2;
        printf("cut protect: connects           %d/3 last(%d,%d)  %s\n",
               wins, x, y, ok ? "PASS" : "FAIL");
        if(!ok) fails++;
    }

    // Keima push-through: White's side stones at B3 and D2/E2 are
    // linked by a knight's move; Black's newest stone C3 pushes into
    // the gap. White must block at the other waist, C2 = (2,7) —
    // the reported failure was pushing straight through keima and
    // ogeima links on the side.
    const char *keima[9] = {
        ".........",
        "..X...X..",
        ".........",
        ".........",
        "XXXXXXXXX",
        ".........",
        ".OX......",
        "...OO.OO.",
        ".O.O...O.",
    };
    setup(keima, WHITE);
    packedSet(game.prevBoard, 6 * 9 + 2, EMPTY); // (2,6) is the last move
    {
        // Either waist-side block is correct — the failure mode being
        // tested is tenuki while the push walks through.
        uint8_t x, y;
        int wins = 0;
        for(int t = 0; t < 3; t++) {
            srand(1000 + t * 7777);
            wins += trial(2, 7, 3, 6, &x, &y);
        }
        bool ok = wins >= 2;
        printf("keima push: blocks waist        %d/3 last(%d,%d)  %s\n",
               wins, x, y, ok ? "PASS" : "FAIL");
        if(!ok) fails++;
    }


    // Two-space extension wedge: White holds a nikken biraki on the
    // side (B3-E3); Black wedges at C3/D3 territory between them.
    // White must respond locally (attach/block) — each gap point is
    // adjacent to one chain and within distance 2 of the other, the
    // link detector's exact trigger.
    const char *wedge[9] = {
        ".........",
        "..X...X..",
        ".........",
        ".........",
        "XXXXXXXXX",
        ".........",
        ".........",
        ".O.XO.OO.",
        ".O..O..O.",
    };
    setup(wedge, WHITE);
    packedSet(game.prevBoard, 7 * 9 + 3, EMPTY); // wedge (3,7) is last
    {
        uint8_t x, y;
        int wins = 0;
        for(int t = 0; t < 3; t++) {
            srand(1000 + t * 7777);
            ai.reset();
            ai.think(game);
            x = y = 0xFF;
            if(ai.bestMove(game, x, y)) {
                int ddx = x > 3 ? x - 3 : 3 - x;
                int ddy = y > 7 ? y - 7 : 7 - y;
                wins += (ddx <= 1 && ddy <= 1);
            }
        }
        bool ok = wins >= 2;
        printf("2-space wedge: answers locally  %d/3 last(%d,%d)  %s\n",
               wins, x, y, ok ? "PASS" : "FAIL");
        if(!ok) fails++;
    }


    // Elephant jump wedge: White's hazama tobi (C2 to E4, diagonal
    // two-space) gets wedged at the hazama point D3 = (3,6). The
    // center is DIAGONAL to both chains, so the link prior cannot
    // guard it proactively — but the response points around the
    // wedge are orthogonally adjacent to one chain with the other
    // within 2, so the fight must be answered locally.
    const char *hazama[9] = {
        ".........",
        "..X...X..",
        ".........",
        ".........",
        "XXXXXXXXX",
        "....O.OO.",
        "...X.....",
        "..O...O..",
        "..O..O.O.",
    };
    setup(hazama, WHITE);
    packedSet(game.prevBoard, 6 * 9 + 3, EMPTY); // wedge (3,6) is last
    {
        uint8_t x, y;
        int wins = 0;
        for(int t = 0; t < 3; t++) {
            srand(1000 + t * 7777);
            ai.reset();
            ai.think(game);
            x = y = 0xFF;
            if(ai.bestMove(game, x, y)) {
                int ddx = x > 3 ? x - 3 : 3 - x;
                int ddy = y > 6 ? y - 6 : 6 - y;
                wins += (ddx <= 1 && ddy <= 1);
            }
        }
        bool ok = wins >= 2;
        printf("elephant wedge: answers locally %d/3 last(%d,%d)  %s\n",
               wins, x, y, ok ? "PASS" : "FAIL");
        if(!ok) fails++;
    }


    // From a real saved game (ardugo_20260727_015634.sgf, move 14):
    // Black's D5 push threatens to sever White's upper-left group
    // from the D4 stone. White must hold the junction C5 = (2,4) —
    // in the game White played C4 instead and Black cut at C5,
    // walking through the wall. 13 stones on board: this is also the
    // regression test for the over-broad mid-game gate that had
    // disabled all push defense before move ~20.
    const char *wallcut[9] = {
        ".........",
        ".........",
        "..OOX....",
        "..OXX.X..",
        "...X.XO..",
        "...O.XO..",
        ".........",
        ".........",
        ".........",
    };
    setup(wallcut, WHITE);
    packedSet(game.prevBoard, 4 * 9 + 3, EMPTY); // D5 (3,4) is the push
    {
        uint8_t x, y;
        int wins = 0;
        for(int t = 0; t < 3; t++) {
            srand(1000 + t * 7777);
            wins += trial(2, 4, 2, 4, &x, &y);
        }
        bool ok = wins >= 2;
        printf("wall junction: white holds C5   %d/3 last(%d,%d)  %s\n",
               wins, x, y, ok ? "PASS" : "FAIL");
        if(!ok) fails++;
    }

    printf(fails ? "\n%d FAILURE(S)\n" : "\nall passed\n", fails);
    return fails;
}
