#include "jaylib.h"
#include "utils.h"
#include "constants.h"
#include "game.h"
#include "ai.h"
#include "display.h"

Jaylib jay;
Game game;
AI ai;
Display display(jay, game);

uint8_t stage = STAGE_TITLE;
uint8_t menuCursor = 0;
uint8_t inverted = 1;
uint8_t aiTimer = 0;

uint8_t isAITurn() {
    return game.mode == MODE_VS_AI && game.turn == game.aiPlayer;
}

void startGame() {
    game.reset();
    ai.reset();
    aiTimer = 30;
    stage = STAGE_PLAY;
}

void renderGameView() {
    display.renderBoard();
    display.renderCursor();
    display.renderInfo();
}

void endTurnCheck() {
    if(game.isGameOver()) {
        game.computeScore();
        stage = STAGE_SCORING;
    }
}

// RAM 0x800-0x801 (MAGIC_KEY_POS) can be overwritten by hardware/USB
// while the sketch runs. The flood stack and MCTS pool redirect any
// element at that address to spare slots (see floodSlot/node); the
// screen and playout sim buffers can absorb a hit. Game state and the
// book walker must not sit there — halt loudly so it gets noticed.
uint8_t spansMagicKey(const void *p, uint16_t len) {
    uint16_t a = (uint16_t)p;
    return a <= 0x801 && a + len > 0x800;
}

void checkMagicKeyHazard() {
    if(spansMagicKey(&game, sizeof(Game)) ||
       spansMagicKey(&ai, sizeof(AI))) {
        jay.smallPrintPgm(10, 28, F("0800 HAZARD: SHUFFLE RAM"), 1);
        jay.display();
        while(1) {}
    }
}

void setup() {
    jay.boot();
    // Hold UP at power-on for safe-upload mode: this sketch constantly
    // writes the buffers that overlap the bootloader magic key at
    // 0x800, which breaks the normal auto-reset upload.
    jay.flashlight();
    jay.invert(1);
    jay.clear();
    jay.initRandomSeed();

    checkMagicKeyHazard();

    //jay.smallPrint(99, 46, itoa((uint16_t)&game, 16), 1);
    //jay.smallPrint(99, 56, itoa((uint16_t)&ai, 16), 1);
    //jay.display();
    //while(1) {};
}

void loop() {
    if(!jay.nextFrame()) return;

    jay.clear();
    jay.pollButtons();

    switch(stage) {
    case STAGE_TITLE:
        if(jay.justPressed(UP_BUTTON) && menuCursor > 0) menuCursor--;
        if(jay.justPressed(DOWN_BUTTON) && menuCursor < 3) menuCursor++;
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
                if(!ai.chooseMove(game)) {
                    // Blocking search borrows the screen buffer as its
                    // node pool; the OLED keeps showing this frame.
                    // No cursor: it would sit frozen mid-blink.
                    display.renderBoard();
                    display.renderInfo();
                    jay.smallPrintPgm(66, 40, F("THINKING..."), 1);
                    jay.display();
                    ai.think(game);
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
            if(jay.justPressed(UP_BUTTON) && game.cursorY > 0) game.cursorY--;
            if(jay.justPressed(DOWN_BUTTON) && game.cursorY < BOARD_SIZE - 1) game.cursorY++;
            if(jay.justPressed(LEFT_BUTTON) && game.cursorX > 0) game.cursorX--;
            if(jay.justPressed(RIGHT_BUTTON) && game.cursorX < BOARD_SIZE - 1) game.cursorX++;
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
        jay.drawPromptPgm(22, F("PASS?"), 0);
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
        if(jay.justPressed(A_BUTTON) || jay.justPressed(B_BUTTON)) {
            stage = STAGE_GAME_OVER;
        }
        break;

    case STAGE_GAME_OVER:
        display.renderBoard();
        display.renderGameOver();
        if(jay.justPressed(A_BUTTON) || jay.justPressed(B_BUTTON)) {
            stage = STAGE_TITLE;
        }
        break;

    case STAGE_HELP:
        display.renderHelp();
        if(jay.justPressed(A_BUTTON) || jay.justPressed(B_BUTTON))
            stage = STAGE_TITLE;
        break;
    }

    jay.display();
}

// vim:syntax=c
