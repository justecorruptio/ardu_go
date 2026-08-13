#include "jaylib.h"
#include "utils.h"
#include "constants.h"
#include "game.h"
#include "ai.h"
#include "display.h"

Jaylib jay;
// The bootloader magic key lives at RAM 0x800-0x801 and hardware can
// stomp it; game/ai must never sit there (checkMagicKeyHazard halts
// loudly if they do — it fired after a RAM-layout change). This
// stomp-tolerant pad shifts them clear; retune the size if the
// hazard screen ever reappears, and verify with test/checkmagic.sh.
// (Retuned 46 -> 34 in the 2026-08-02 RAM hunt: the minimum where the
// 0x800 clearance AND the boardAt/markPtr lo8 bounds all hold --
// empirically swept per layout change - EVERY .data/.bss size change
// upstream of simBoard/simMark moves their lo8 and needs a re-sweep;
// small RAM savings often get eaten by the required pad growth (the
// modular tax). checkmagic.sh verifies every constraint, EVERY build.)
// (48: retuned 2026-08-13 -- the incremental near-mask's descentCaptured
// byte shifted sBuffer up 1 and put node 136 on the magic key.)
uint8_t magicPad[48] __attribute__((used));
Game game;
AI ai;
Display display(jay, game);

uint8_t stage = STAGE_TITLE;

// Boot straight into the score screen with a real finished game
// (Jay's 2026-08-01 save, B+0.5 after komi) — for score-screen layout
// iteration. Comment out for the normal title boot.
//#define BOOT_SCORE_DEMO
#ifdef BOOT_SCORE_DEMO
static const uint8_t PROGMEM DEMO_GAME[] = {
    0x21, 0x30, 0x3B, 0x1E, 0x17, 0x16, 0x0D, 0x15, 0x0C, 0x0B, 0x28,
    0x31, 0x32, 0x43, 0x3A, 0x39, 0x44, 0x4C, 0x4D, 0x42, 0x27, 0x26,
    0x29, 0x0E, 0x0F, 0x1F, 0x20, 0x14, 0x02, 0x01, 0x05, 0x03, 0x04,
    0x02};
#endif
uint8_t menuCursor = 0;
uint8_t inverted = 1;
uint8_t aiTimer = 0;
uint16_t aiThinkMs = 0; // 0 = no search stats to show

uint8_t isAITurn() {
    return game.mode == MODE_VS_AI && game.turn == game.aiPlayer;
}

void startGame() {
    game.reset();
    ai.reset();
    aiTimer = 30;
    aiThinkMs = 0;
    stage = STAGE_PLAY;
}

void renderGameView() {
    display.renderBoard();
    display.renderCursor();
    display.renderInfo();

    // Middle info section: last search's stats (replaces the
    // AI THINKING... message once the search is done):
    // "WIN:64% 62 SEC" / "VISITS:64/600". Panel fits 15 chars, so
    // 100% displays as 99.
    if(game.mode == MODE_VS_AI && aiThinkMs) {
        jay.smallPrintPgm(66, 35, F("WIN:"), 1);
        uint8_t x = jay.prNum(66 + 16, 35,
                          ai.statPct > 99 ? 99 : ai.statPct);
        jay.smallPrintPgm(x, 35, F("%"), 1);
        x = jay.prNum(x + 8, 35, (aiThinkMs + 500) / 1000);
        jay.smallPrintPgm(x, 35, F(" SEC"), 1);

        jay.smallPrintPgm(66, 41, F("VISITS:"), 1);
        x = jay.prNum(66 + 28, 41, ai.statVisits);
        jay.smallPrintPgm(x, 41, F("/"), 1);
        jay.prNum(x + 4, 41, ai.statTotal);
    }
}

void endTurnCheck() {
    if(game.isGameOver()) {
        ai.scoreDead(game); // playout-vote dead stones off, then score
        stage = STAGE_SCORING;
    }
}

// RAM 0x800-0x801 (MAGIC_KEY_POS) can be overwritten by hardware/USB
// while the sketch runs, so game/ai state must never sit there (the
// flood stack and MCTS pool land in tolerant buffers; the screen and
// playout sim buffers can absorb a hit). This invariant is enforced at
// BUILD time by test/checkmagic.sh, which asserts from the ELF that
// every critical region clears 0x800 — it replaced the old runtime
// startup check (redundant: game/ai addresses are fixed at link time).

void setup() {
    jay.boot();
    // Hold UP at power-on for safe-upload mode. This sketch constantly
    // writes the buffers that overlap the bootloader magic key at
    // 0x800; the vendored CDC_Setup (usb_cdc_v.cpp) now parks the CPU
    // after arming the auto-reset so the key survives, but flashlight
    // stays as the fallback if that path ever breaks.
    jay.flashlight();
    jay.invert(1);
    jay.clear();
    // Seed the engine's xorshift directly. Vendored (2026-08-02): the
    // Arduboy2 generateRandomSeed() mixes an ADC read with micros(),
    // and that micros() reference was the last thing keeping the stock
    // wiring.c.o linked (multiple-definition wall against wiring_v.c).
    // Same entropy sources, sub-ms timer bits from raw TCNT0 instead:
    extern uint16_t rngState;
    power_adc_enable();
    ADCSRA |= _BV(ADSC);                       // unconnected-pin read
    while(bit_is_set(ADCSRA, ADSC)) { }
    rngState = (uint16_t)((ADC << 8) ^ millis() ^ TCNT0) | 1;
    power_adc_disable();

    //jay.smallPrint(99, 46, itoa((uint16_t)&game, 16), 1);
    //jay.smallPrint(99, 56, itoa((uint16_t)&ai, 16), 1);
    //jay.display();
    //while(1) {};

    // RAM-layout guard: halt loud if a layout drift put the node pool on
    // the 0x800 magic key or broke boardAt's carry-free bound. The old
    // startup check was cut as "redundant with checkmagic.sh" -- but that
    // silently went stale and the layout drifted anyway, so it's back.
    if(uint8_t hz = ai.layoutHazard()) {
        jay.clear();
        jay.smallPrintPgm(14, 26, F("RAM LAYOUT ERR"), 1);
        jay.smallPrint(58, 36, itoa(hz, 10), 1);
        jay.display();
        while(1) { }
    }

#ifdef BOOT_SCORE_DEMO
    game.reset();
    for(uint8_t i = 0; i < sizeof(DEMO_GAME); i++) {
        uint8_t m = pgm_read_byte(DEMO_GAME + i);
        game.playMove(m % 9, m / 9);
    }
    game.pass();
    game.pass();               // double pass = game over
    ai.reset();
    ai.scoreDead(game);        // dead stones off + real territory
    stage = STAGE_SCORING;     // A advances to GAME_OVER, then title
#endif
}

// A or B pressed: the confirm/dismiss idiom used by the scoring,
// game-over, and help stages.
static bool anyAB() { return jay.justPressed(A_BUTTON) || jay.justPressed(B_BUTTON); }

// Button-driven clamp step: dec if >0, inc if <hi. Shared by the menu
// cursor and the board cursor (both axes).
static void stepClamp(uint8_t decBtn, uint8_t incBtn, uint8_t &v, uint8_t hi) {
    if(jay.justPressed(decBtn) && v > 0) v--;
    if(jay.justPressed(incBtn) && v < hi) v++;
}

void loop() {
    if(!jay.nextFrame()) return;

    jay.clear();
    jay.pollButtons();

    switch(stage) {
    case STAGE_TITLE:
        stepClamp(UP_BUTTON, DOWN_BUTTON, menuCursor, 3);
        if(jay.justPressed(A_BUTTON)) {
            switch(menuCursor) {
            case 0: // VS AI
                game.mode = MODE_VS_AI;
                game.aiPlayer = WHITE;
                startGame();
                break;
            case 1: // VS HUMAN
                game.mode = MODE_VS_HUMAN;
                startGame();
                break;
            case 2: // SHOW RULES
                stage = STAGE_HELP;
                break;
            case 3: // INVERT SCREEN
                inverted ^= 1;
                jay.invert(inverted);
                break;
            }
        }
        display.renderTitle(menuCursor);
        break;

    case STAGE_PLAY:
        if(isAITurn()) {
            if(--aiTimer == 0) {
                aiTimer = 30;
                if(ai.chooseMove(game)) {
                    aiThinkMs = 0; // book move: no search, no stats
                } else {
                    // Blocking search borrows the screen buffer as its
                    // node pool; the OLED keeps showing this frame (no
                    // cursor -- it would sit frozen mid-blink). The frame
                    // is cleared inside renderThinkFrame so the borrowed
                    // opening scratch / tree wreckage never shows.
                    display.renderThinkFrame();
                    jay.display();
                    uint16_t t0 = (uint16_t)millis();
                    ai.think(game);
                    aiThinkMs = (uint16_t)millis() - t0;
                    if(ai.resigned) {
                        game.resignedBy = game.aiPlayer;
                        stage = STAGE_GAME_OVER;
                        jay.clear();
                        break;
                    }
                    uint8_t x, y;
                    if(ai.bestMove(game, x, y)) {
                        game.playMove(x, y);
                        ai.notifyMove(x, y);
                    } else {
                        game.pass();
                        ai.notifyPass();
                        endTurnCheck();
                    }
                    // The buffer holds tree wreckage and this frame
                    // falls through to renderGameView + display —
                    // clear now that bestMove is done reading the tree
                    jay.clear();
                }
            }
        } else {
            stepClamp(UP_BUTTON, DOWN_BUTTON, game.cursorY, BOARD_SIZE - 1);
            stepClamp(LEFT_BUTTON, RIGHT_BUTTON, game.cursorX, BOARD_SIZE - 1);
            if(jay.justPressed(A_BUTTON)) {
                if(game.playMove(game.cursorX, game.cursorY)) {
                    ai.notifyMove(game.cursorX, game.cursorY);
                    aiTimer = 30;
                }
            }
            if(jay.justPressed(B_BUTTON)) {
                stage = STAGE_PASS_CONFIRM;
            }
        }
        renderGameView();
        break;

    case STAGE_PASS_CONFIRM:
        renderGameView();
        // "PASS?" as 1-based large-font glyph numbers (P A S S ?)
        static const char PASS_STR[] PROGMEM = {16, 4, 18, 18, 3, 0};
        jay.drawPromptPgm(22, (const __FlashStringHelper *)PASS_STR, 0);
        // Key hint under the prompt (box ends y=38), on a cleared
        // band so it reads over the board; prompt interior is 1,
        // prompt text 0 — match.
        // 12 glyphs x4 + 2 spaces x2 - 1 = 51 px (spaces advance 2)
        for(uint8_t i = 36; i < 91; i++)
            jay.drawFastVLine(i, 39, 9, 1);
        jay.smallPrintPgm(38, 41, F("A:OK  B:CANCEL"), 0);
        if(jay.justPressed(A_BUTTON)) {
            game.pass();
            ai.notifyPass();
            aiTimer = 30;
            stage = STAGE_PLAY;
            endTurnCheck();
        }
        if(jay.justPressed(B_BUTTON)) {
            stage = STAGE_PLAY;
        }
        break;

    case STAGE_SCORING:
        display.renderBoard();
        display.renderScoring();
        if(anyAB()) {
            stage = STAGE_GAME_OVER;
        }
        break;

    case STAGE_GAME_OVER:
        display.renderBoard();
        display.renderGameOver();
        if(anyAB()) {
            stage = STAGE_TITLE;
        }
        break;

    case STAGE_HELP:
        display.renderHelp();
        if(anyAB())
            stage = STAGE_TITLE;
        break;
    }

    jay.display();
}

// vim:syntax=c
