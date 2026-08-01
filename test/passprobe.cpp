// Jay's 2026-08-01 game: replay to his first pass (move 33, 31
// stones, partitioned board), engine (White) to move. Expect PASS.
#include <stdio.h>
#include <string.h>
#include "../game.cpp"
#include "../ai.cpp"
uint8_t Arduboy2Base::sBuffer[1024];
static const char *MOVES =
    "gd df fg dd ee de ed ef ff dc ec ch eh eb fb ea fa fc gc fe "
    "fd db ge dh fh bc eg dg di ci ei hb";
int main() {
    Game game; AI ai;
    game.reset(); ai.reset();
    int n = 0;
    for(const char *p = MOVES; p[0] && p[1]; n++) {
        uint8_t x = p[0] - 'a', y = p[1] - 'a';
        game.playMove(x, y);
        ai.notifyMove(x, y);
        p += 2;
        while(*p == ' ') p++;
    }
    game.pass();          // Jay passes (move 33)
    ai.notifyPass();
    printf("replayed %d moves + pass, stones=%u, turn=%s\n",
           n, countStones(game),
           game.turn == WHITE ? "WHITE(engine)" : "BLACK");
    printf("settleVote = %d (SETTLE_NONE=%d)\n", settleVote(game), SETTLE_NONE);
    ai.think(game);
    uint8_t x, y;
    if(ai.bestMove(game, x, y))
        printf("engine plays %c%d  (WRONG: should pass)\n",
               'A' + x + (x >= 8 ? 1 : 0), 9 - y);
    else
        printf("engine PASSES -> game ends on the honest count\n");
    return 0;
}
