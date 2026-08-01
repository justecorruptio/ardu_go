// hunt c11: replay game 5328 to the mv54 decision, dump root beliefs
#include <stdio.h>
#include <string.h>
#include "../game.cpp"
#include "../ai.cpp"
uint8_t Arduboy2Base::sBuffer[1024];
static const char *MOVES =
    "ee cf dg cc gf cg dd dc ec gc dh he cd be bd ch bh bi gb fb "
    "eb hb bc hf hg gg ff gh di ci hh gi bg ah ge hd fc ga df ae "
    "ag ce ai gd cb fd ii ig ed ad db ac ab ea";
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
    printf("replayed %d moves, turn=%s\n", n,
           game.turn == BLACK ? "BLACK" : "WHITE");
    ai.think(game);
    printf("root (GTP coords, row=9-y):\n");
    for(uint8_t c = node(0).firstChild; c != 0xFF; c = node(c).nextSibling) {
        uint8_t lat = node(c).move & 0x80;
        uint16_t v = nVisits(c), w = nWins(c);
        uint8_t m = node(c).move & 0x7F;
        if(m > 81) continue;
        if(!lat && v < 15 && m != 27+3) continue;
        char col = (m == 81) ? 'P' : 'A' + (m % 9) + ((m % 9) >= 8 ? 1 : 0);
        printf("  %c%d v=%3u w=%3u q=%2u%%%s\n",
               col, m == 81 ? 0 : 9 - (m / 9),
               v, w, v ? 100 * w / v : 0, lat ? "  LATENT" : "");
    }
    // the two contenders: D9 = (3,0) pos 3; B6(B4 gtp) = (1,5) pos 46
    for(uint8_t i = 0; i < BOARD_CELLS; i++)
        simBoard[i] = packedGet(game.board, i);
    buildChainMap();
    printf("prior(D9) = %+d  prior(B4gtp) = %+d\n",
           candidatePrior(3, game.turn, 4, 0),
           candidatePrior(5 * 9 + 1, game.turn, 4, 0));
    printf("rootVitals: n=%u [%u %u %u]\n", nRootVitals,
           rootVitals[0], rootVitals[1], rootVitals[2]);
#ifdef LD_CRIT
    {
        for(uint8_t i = 0; i < BOARD_CELLS; i++)
            simBoard[i] = packedGet(game.board, i);
        ldClassify();
        printf("LD complexes:");
        for(uint8_t i = 1; i < 64; i++)
            if(ldFind(i) == i && ldStatus[i] != LD_NONE) {
                uint8_t m = ldMove[i];
                printf(" [id%u %s", i,
                       ldStatus[i] == LD_ALIVE ? "ALIVE" :
                       ldStatus[i] == LD_DEAD ? "DEAD" : "CRIT");
                if(ldStatus[i] == LD_CRIT && m < BOARD_CELLS)
                    printf(" mv=%c%d", 'A' + (m % 9) + ((m % 9) >= 8 ? 1 : 0),
                           9 - (m / 9));
                printf("]");
            }
        printf("\n");
    }
#endif
#ifdef FORCED_EXT
    if(fxChallenger != 0xFF) {
        uint8_t fm = node(fxChallenger).move & 0x7F;
        printf("fxChallenger=%u move=%c%d fxForcedCount=%u\n", fxChallenger,
               'A' + (fm % 9) + ((fm % 9) >= 8 ? 1 : 0), 9 - (fm / 9),
               fxForcedCount);
    } else printf("fxChallenger=none\n");
#endif
    printf("thinkSims=%u wins=%u (%d%%)\n", thinkSims, thinkSimWins,
           thinkSims ? (int)(100L * thinkSimWins / thinkSims) : 0);
    return 0;
}
