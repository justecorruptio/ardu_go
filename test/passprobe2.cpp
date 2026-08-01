// Jay's 07:02 game: replay through B J6 [id] (White 100%/est+2,
// Black filling hopelessly). Engine (White) should PASS, not answer.
#include <stdio.h>
#include <string.h>
#include <string>
#include "../game.cpp"
#include "../ai.cpp"
uint8_t Arduboy2Base::sBuffer[1024];
static const char *MOVES =
    "fc df fe fg dd cd cc gf ge dg he de hf dc hg bc ed cb eb gh "
    "hh be ff dh gg ec fd eg db fh cc dc da ee gi bg fi ei hi gb "
    "fb ef ec ba cc bd dc di ca bb id";
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
    // prime the previous-think eval the gate reads
    thinkSims = 400; thinkSimWins = 400;
    printf("replayed %d moves, stones=%u, turn=%s\n",
           n, countStones(game), game.turn == WHITE ? "WHITE(engine)" : "BLACK");
    printf("settleVote = %d\n", settleVote(game));
    printf("rootLast=%u own[rootLast]=%u (bands: dead<=%d alive>=%d)\n",
           rootLast, Arduboy2Base::sBuffer[rootLast],
           SCORE_PLAYOUTS / 4, 3 * SCORE_PLAYOUTS / 4);
    printf("prevEval gate: sims=%u wins=%u\n", thinkSims, thinkSimWins);
    ai.think(game);
    uint8_t x, y;
    if(ai.bestMove(game, x, y))
        printf("engine plays %c%d  (should PASS)\n",
               'A' + x + (x >= 8 ? 1 : 0), 9 - y);
    else
        printf("engine PASSES (correct: decided game, stop answering)\n");
    return 0;
}
