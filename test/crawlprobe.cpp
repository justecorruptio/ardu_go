// D1 crawl probe: for each input position, run a full think() and dump the
// root children (active + latent) so the analyzer can classify each crawl
// blunder as starved / scored-worse / close-call at the root.
// stdin lines:  <id> <seed> <ntoks> <tok...>   tok = "xy" digits (x,y in 0-8)
//               or "PP" for pass; moves alternate from Black.
// stdout:  POS <id> turn=<B|W> chosen=<idx|255>
//          C <move> <a|L> <visits> <wins>
#include <cstdio>
#include <cstring>
#include "../game.cpp"
#include "../ai.cpp"
uint8_t Arduboy2Base::sBuffer[1024];
static Game game;
static AI ai;
int main() {
    char id[64];
    unsigned long seed;
    int ntoks;
    while(scanf("%63s %lu %d", id, &seed, &ntoks) == 3) {
        game.reset();
        ai.reset();
        for(int i = 0; i < ntoks; i++) {
            char tok[8];
            scanf("%7s", tok);
            if(tok[0] == 'P') game.pass();
            else game.playMove(tok[0] - '0', tok[1] - '0');
        }
        rngState = (uint16_t)seed | 1;
        ai.think(game);
        uint8_t x, y;
        uint8_t got = ai.bestMove(game, x, y);
        printf("POS %s turn=%c chosen=%u\n", id,
               game.turn == BLACK ? 'B' : 'W',
               got ? (unsigned)(y * 9 + x) : 255);
        for(uint8_t c = node(0).firstChild; c != 0xFF; c = node(c).nextSibling)
            printf("C %u %c %u %u\n", node(c).move & 0x7F,
                   (node(c).move & 0x80) ? 'L' : 'a',
                   nVisits(c), nWins(c));
        fflush(stdout);
    }
    return 0;
}
