// hunt diagnosis: replay game 5110 to mv40, dump root beliefs
#include <stdio.h>
#include <string.h>
#include "../game.cpp"
#include "../ai.cpp"
uint8_t Arduboy2Base::sBuffer[1024];
static const char *MOVES =
    "df dd fe gg fc he ce cd fg gh cb fh dc eg cg be ef bc ed bf "
    "gf bg hf ch dg eh hg dh de cc db bb hh ba gb hi ih ff ca fg";
int main() {
    Game game; AI ai;
    game.reset(); ai.reset();
    int n = 0;
    for(const char *p = MOVES; *p; p += 3, n++) {
        uint8_t x = p[0] - 'a', y = p[1] - 'a';
        game.playMove(x, y);
        ai.notifyMove(x, y);
    }
    printf("replayed %d moves, turn=%s\n", n,
           game.turn == BLACK ? "BLACK" : "WHITE");
    ai.think(game);
    printf("root children (move visits wins q%%):\n");
    for(uint8_t c = node(0).firstChild; c != 0xFF; c = node(c).nextSibling) {
        uint8_t lat = node(c).move & 0x80;
        uint16_t v = nVisits(c), w = nWins(c);
        uint8_t m = node(c).move & 0x7F;
        printf("  %c%d v=%3u w=%3u q=%2u%%%s%s\n",
               'A' + (m % 9) + ((m % 9) >= 8 ? 1 : 0), m / 9 + 1,
               v, w, v ? 100 * w / v : 0,
               lat ? "  LATENT" : "",
               v >= POISONED ? "  POISONED" : "");
    }
    // E5's prior in this position
    for(uint8_t i = 0; i < BOARD_CELLS; i++)
        simBoard[i] = packedGet(game.board, i);
    buildChainMap();
    printf("prior(E5) = %+d   prior(J1) = %+d\n",
           candidatePrior(4 * 9 + 4, game.turn, 5 * 9 + 5, 0),
           candidatePrior(0 * 9 + 8, game.turn, 5 * 9 + 5, 0));
    printf("thinkSims=%u wins=%u (%d%%)\n", thinkSims, thinkSimWins,
           thinkSims ? (int)(100L * thinkSimWins / thinkSims) : 0);
    return 0;
}
