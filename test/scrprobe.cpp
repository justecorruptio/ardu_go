// Host screen probe: render every Display screen into the stubbed
// sBuffer over fixed game states and dump them as hex. Diff across UI
// refactors = pixel-identity proof without the emulator.
#include <cstdio>
#include <cstring>
#include "../game.cpp"
#include "../utils.cpp"
#include "../jaylib.cpp"
#include "../display.cpp"
uint8_t Arduboy2Base::sBuffer[1024];
static Jaylib jay;
static Game game;
static Display disp(jay, game);

static void dump(const char *name) {
    printf("%s:", name);
    for(int i = 0; i < 1024; i++) printf("%02x", Arduboy2Base::sBuffer[i]);
    printf("\n");
    memset(Arduboy2Base::sBuffer, 0, 1024);
}

int main() {
    game.reset();
    // a few moves so boards/captures are non-trivial
    const uint8_t mv[][2] = {{4,4},{2,2},{6,2},{2,6},{6,6},{4,2},{2,4},{6,4},{3,3}};
    for(auto &m : mv) game.playMove(m[0], m[1]);
    game.captures[0] = 4; game.captures[1] = 12;
    game.territory[0] = 23; game.territory[1] = 7;

    disp.renderBoard();  dump("board");
    disp.renderCursor(); dump("cursor");
    disp.renderInfo();   dump("info");
    for(uint8_t c = 0; c < 4; c++) {
        jay.counter = 7;
        disp.renderTitle(c);
        char n[16]; snprintf(n, sizeof n, "title%d", c);
        dump(n);
    }
    disp.renderHelp();    dump("help");
    disp.renderScoring(); dump("scoring");
    disp.renderGameOver();dump("gameover_win");
    game.resignedBy = WHITE;
    disp.renderGameOver();dump("gameover_res");
    // second value regime: 3-digit / 1-digit mixes for scoreNum paths
    game.resignedBy = 0;
    game.captures[0] = 0; game.captures[1] = 105;
    game.territory[0] = 1; game.territory[1] = 0;
    disp.renderScoring(); dump("scoring2");
    disp.renderInfo();    dump("info2");

    // Regression (2026-08): the AI-think frame is held on the OLED while
    // think() overwrites sBuffer, so it must be identical whether the
    // buffer started clean or full of the borrowed opening scratch /
    // prior tree wreckage. renderThinkFrame clears internally; if that
    // clear is ever dropped, this FAILs.
    memset(Arduboy2Base::sBuffer, 0xEE, 1024);   // simulate borrowed-scratch garbage
    disp.renderThinkFrame();
    static uint8_t dirtyStart[1024];
    memcpy(dirtyStart, Arduboy2Base::sBuffer, 1024);
    dump("thinkframe");                          // (dumps, then clears to 0)
    disp.renderThinkFrame();                      // now from a clean buffer
    printf("thinkframe dirty-independent: %s\n",
           memcmp(dirtyStart, Arduboy2Base::sBuffer, 1024) == 0
               ? "PASS" : "*** FAIL: held frame leaks buffer trash ***");
    return 0;
}
