// AVR cycle-benchmark firmware for the ProjectABE emulator harness.
// Skips the ArduGo display/USB boot ceremony: think() is pure
// compute and borrows sBuffer (static storage from Arduboy2Base), so
// no boot() is needed. Each think() is bracketed by writes to the
// volatile marker; the emulator hooks that address and reads its
// cycle counter, giving a true device think time.
#include "jaylib.h"
#include "game.h"
#include "ai.h"

Jaylib jay;      // storage for Arduboy2Base::sBuffer (the node pool)
Game game;
AI ai;

extern uint16_t rngState;  // engine PRNG; seed it for repeatable runs

// Marker byte. volatile + global => the store compiles to STS, which
// the emulator routes through write() where the harness hooks it.
volatile uint8_t benchMark;

// Benchmark positions. Default: FIVE real positions spanning the game
// (opening 8st / early-mid 13st / the classic 26st midgame / late-mid
// 34st / endgame 45st), rotated one think each — the 5-position MEAN is
// the bench number, making it robust to any single position's stop
// timing (the 08-14 lesson: stable-stop fires on leader stability, so
// one board's churn can swing a single-position bench 20%).
// -DBENCH_SINGLE: the old 26-stone midgame board only (pre-08-14
// numbers compare here). -DBENCH_OPENING: the old 8-stone opening only.
// P2-P4 extracted from chain2 game_2002.sgf (v2full vs 10k human) at
// t=14/36/48 via test/nnretrain/goboard.py.
#if defined(BENCH_OPENING)
#define NPOS 1
static const char P0[] PROGMEM =
    "........."
    ".....O..."
    "..OO....."
    "...XX.O.."
    "........."
    "...XX...."
    "........."
    "........."
    ".........";
static const char* const POSN[1] PROGMEM = {P0};
static const uint8_t PTURN[1] = {BLACK};
#elif defined(BENCH_SINGLE)
#define NPOS 1
static const char P0[] PROGMEM =
    "....XO..."
    "..X.XO.O."
    "....XO..."
    "..X.XOO.."
    "...X.XO.."
    "...O.XO.."
    ".OX..O..."
    "...OO.OO."
    ".O.O...O.";
static const char* const POSN[1] PROGMEM = {P0};
static const uint8_t PTURN[1] = {WHITE};
#else
#define NPOS 5
static const char P0[] PROGMEM =   // opening, 8 stones
    "........."
    ".....O..."
    "..OO....."
    "...XX.O.."
    "........."
    "...XX...."
    "........."
    "........."
    ".........";
static const char P1[] PROGMEM =   // early-mid, 13 stones (t=14)
    "........."
    "........."
    "..X..X..."
    ".......X."
    ".X.OOXXO."
    ".....OO.O"
    ".......O."
    "........."
    ".........";
static const char P2[] PROGMEM =   // the classic midgame, 26 stones
    "....XO..."
    "..X.XO.O."
    "....XO..."
    "..X.XOO.."
    "...X.XO.."
    "...O.XO.."
    ".OX..O..."
    "...OO.OO."
    ".O.O...O.";
static const char P3[] PROGMEM =   // late-mid, 34 stones (t=36)
    "........."
    "...OXX..."
    ".XXOOX..."
    ".OXXO..X."
    "O.OOOXXO."
    ".OXXXOO.O"
    ".XXOO..O."
    ".XO......"
    ".........";
static const char P4[] PROGMEM =   // endgame, 45 stones (t=48)
    ".O.X....."
    "..XOXX..."
    "XXXOOX..."
    ".OXXOX.X."
    "OOOOOXXO."
    "OOXXXOO.O"
    ".XXOO..O."
    "XXO.O...."
    ".........";
static const char* const POSN[5] PROGMEM = {P0, P1, P2, P3, P4};
static const uint8_t PTURN[5] = {BLACK, BLACK, WHITE, BLACK, BLACK};
#endif

static void loadPos(uint8_t p) {
    const char *bp = (const char *)pgm_read_ptr(&POSN[p]);
    game.reset();
    for(uint8_t y = 0; y < 9; y++)
        for(uint8_t x = 0; x < 9; x++) {
            char c = (char)pgm_read_byte(bp + y * 9 + x);
            game.set(x, y, c == 'X' ? BLACK : c == 'O' ? WHITE : EMPTY);
        }
    memcpy(game.prevBoard, game.board, sizeof(game.board));
    game.turn = PTURN[p];
}

void setup() {
    uint8_t p = 0;
    for(;;) {
        loadPos(p);
        p = (uint8_t)((p + 1) % NPOS);

        rngState = 12345;      // identical search every iteration
        benchMark = 0xA0;      // think START
        ai.reset();
        ai.think(game);
        benchMark = 0xA1;      // think END
    }
}

void loop() {}
