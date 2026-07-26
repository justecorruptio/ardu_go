// Direct reliability test of ladderEscapes(): canonical working and
// broken ladders. The PRIOR_FEED/SAVE machinery trusts this reader.
#include <stdio.h>
#include <string.h>

#include "../game.cpp"
#include "../ai.cpp"

uint8_t Arduboy2Base::sBuffer[1024];

static void load(const char *rows[9]) {
    for(uint8_t y = 0; y < 9; y++)
        for(uint8_t x = 0; x < 9; x++) {
            char c = rows[y][x];
            simBoard[y * 9 + x] = c == 'X' ? BLACK : c == 'O' ? WHITE : EMPTY;
        }
}

static int check(const char *name, const char *rows[9],
                 uint8_t defx, uint8_t defy, uint8_t escx, uint8_t escy,
                 uint8_t expect) {
    load(rows);
    uint8_t got = ladderEscapes(defy * 9 + defx, escy * 9 + escx);
    printf("%-34s expect %s got %s  %s\n", name,
           expect ? "escape" : "dies", got ? "escape" : "dies",
           got == expect ? "PASS" : "FAIL");
    return got == expect ? 0 : 1;
}

int main() {
    int f = 0;

    // True shicho: black (4,4) in atari (sole liberty (4,5)),
    // white support at (3,5) makes each extension come out at two
    // liberties. Open board below: the zigzag runs out and black dies.
    const char *shicho[9] = {
        ".........",
        ".........",
        ".........",
        "....O....",
        "...OXO...",
        "...O.....",
        ".........",
        ".........",
        ".........",
    };
    f += check("true shicho: defender dies", shicho, 4, 4, 4, 5, 0);

    // Same shape with a black breaker on the zigzag path: escapes.
    const char *shichoBreak[9] = {
        ".........",
        ".........",
        ".........",
        "....O....",
        "...OXO...",
        "...O.....",
        "......X..",
        ".........",
        ".........",
    };
    f += check("shicho with breaker: escapes", shichoBreak, 4, 4, 4, 5, 1);

    // Breaker far off the path: still dies.
    const char *shichoWrong[9] = {
        ".X.......",
        ".........",
        ".........",
        "....O....",
        "...OXO...",
        "...O.....",
        ".........",
        ".........",
        ".........",
    };
    f += check("off-path breaker: dies", shichoWrong, 4, 4, 4, 5, 0);
 // Defender with a friendly stone ahead on the escape path: escapes.
    const char *friendahead[9] = {
        ".........",
        "....O....",
        "...OXO...",
        ".........",
        "....X....",
        ".........",
        ".........",
        ".........",
        ".........",
    };
    f += check("friend ahead: escapes", friendahead, 4, 2, 4, 3, 1);

    printf(f ? "\n%d FAILURE(S)\n" : "\nall passed\n", f);
    return f;
}
