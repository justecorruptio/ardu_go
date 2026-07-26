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

static int expectMove(const char *name, uint8_t ex, uint8_t ey) {
    ai.think(game);
    uint8_t x = 0xFF, y = 0xFF;
    uint8_t got = ai.bestMove(game, x, y);
    bool ok = got && x == ex && y == ey;
    printf("%-28s expected (%d,%d) got ", name, ex, ey);
    if(got) printf("(%d,%d)", x, y);
    else printf("pass/resign");
    printf("  %s\n", ok ? "PASS" : "FAIL");
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

    printf(fails ? "\n%d FAILURE(S)\n" : "\nall passed\n", fails);
    return fails;
}
