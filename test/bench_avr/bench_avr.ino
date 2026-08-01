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

// Two benchmark positions. Default: the realistic mid-game board
// (same as test/bench.cpp), thinks at the shipped 400 iterations.
// -DBENCH_OPENING: an 8-stone opening (game_002 to move 8, Black to
// play) -- under OPENING_BOOST_STONES the think naturally runs 600
// iterations, so this benches the true opening wait-peak.
#ifdef BENCH_OPENING
static const char rows[9][10] = {
    ".........",
    ".....O...",
    "..OO.....",
    "...XX.O..",
    ".........",
    "...XX....",
    ".........",
    ".........",
    ".........",
};
#else
static const char rows[9][10] = {
    "....XO...",
    "..X.XO.O.",
    "....XO...",
    "..X.XOO..",
    "...X.XO..",
    "...O.XO..",
    ".OX..O...",
    "...OO.OO.",
    ".O.O...O.",
};
#endif

void setup() {
    game.reset();
    for(uint8_t y = 0; y < 9; y++)
        for(uint8_t x = 0; x < 9; x++) {
            char c = rows[y][x];
            game.set(x, y, c == 'X' ? BLACK : c == 'O' ? WHITE : EMPTY);
        }
    for(uint8_t i = 0; i < 81; i++) {} // (prevBoard already copied by set? no)
    memcpy(game.prevBoard, game.board, sizeof(game.board));
#ifdef BENCH_OPENING
    game.turn = BLACK;
#else
    game.turn = WHITE;
#endif

    for(;;) {
        rngState = 12345;      // identical search every iteration
        benchMark = 0xA0;      // think START
        ai.reset();
        ai.think(game);
        benchMark = 0xA1;      // think END
    }
}

void loop() {}
