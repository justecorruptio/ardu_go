#include "display.h"
#include "utils.h"

Display::Display(Jaylib &_jay, Game &_game)
:jay(_jay), game(_game) {}

void Display::renderBoard() {
    // Draw grid lines
    for(uint8_t i = 0; i < BOARD_SIZE; i++) {
        jay.drawFastHLine(GRID_LEFT, GRID_TOP + i * CELL_SIZE, (BOARD_SIZE - 1) * CELL_SIZE + 1);
        jay.drawFastVLine(GRID_LEFT + i * CELL_SIZE, GRID_TOP, (BOARD_SIZE - 1) * CELL_SIZE + 1);
    }

    // Draw star points (hoshi) for 9x9 as 3x3 squares
    static const uint8_t PROGMEM hoshi[][2] = {{2,2},{6,2},{4,4},{2,6},{6,6}};
    for(uint8_t i = 0; i < 5; i++) {
        uint8_t x = GRID_LEFT + pgm_read_byte(&hoshi[i][0]) * CELL_SIZE;
        uint8_t y = GRID_TOP + pgm_read_byte(&hoshi[i][1]) * CELL_SIZE;
        static const uint8_t PROGMEM HOSHI3[3] = {0x07, 0x07, 0x07};
        jay.drawBand(x - 1, y - 1, HOSHI3, 3);   // 3x3 filled square
    }

    // Draw stones, folding in the last-move "v" on the newest stone
    // (present now, empty in prevBoard) in the same pass -- inverted so
    // it reads on either color. Only the last move has prevBoard EMPTY
    // (captures leave the cell EMPTY -> skipped); lastDrawn keeps the
    // old first-match-only semantics.
    uint8_t lastDrawn = 0;
    for(uint8_t gy = 0; gy < BOARD_SIZE; gy++) {
        for(uint8_t gx = 0; gx < BOARD_SIZE; gx++) {
            uint8_t cell = game.at(gx, gy);
            if(cell == EMPTY) continue;
            uint8_t x = GRID_LEFT + gx * CELL_SIZE - 3;
            uint8_t y = GRID_TOP + gy * CELL_SIZE - 3;
            if(cell == BLACK) {
                jay.drawBand(x, y, GLYPH_STONE_FILLED, 7);
            } else {
                jay.drawBand(x, y, GLYPH_STONE_FILLED, 7, 0); // clear interior
                jay.drawBand(x, y, GLYPH_STONE_OUTLINE, 7);
            }
            if(!lastDrawn && packedGet(game.prevBoard, gy * BOARD_SIZE + gx) == EMPTY) {
                lastDrawn = 1;
                jay.drawBand(x + 2, y + 3, GLYPH_LASTMOVE, 3, cell == BLACK ? 0 : 1);
            }
        }
    }
}

void Display::renderCursor() {
    uint8_t x = GRID_LEFT + game.cursorX * CELL_SIZE;
    uint8_t y = GRID_TOP + game.cursorY * CELL_SIZE;
    // Wall-clock blink (~256 ms phase), NOT the frame counter: after
    // a blocking think() the frame scheduler catches up in a burst of
    // instant frames, which made a frame-based blink stutter.
    uint8_t blink = (uint8_t)(millis() >> 8) & 1;
    if(blink)
        jay.drawBand(x - 3, y - 3, GLYPH_CURSOR, 7);
}

// Static UI labels, table-driven: each print call site cost ~16B of
// argument setup + F() plumbing; one PROGMEM table + walker renders
// the same pixels. Bit 7 of x selects largePrintPgm. Screens address
// their slice by [from,to) constants. Strings and coordinates are
// unchanged from the hand-written versions (scrprobe-verified).
struct ScLabel { uint8_t x, y; const char *s; };
static const char SC_S0[] PROGMEM = "SCORING";
static const char SC_S1[] PROGMEM = "WHT";
static const char SC_S2[] PROGMEM = "BLK";
static const char SC_S3[] PROGMEM = "AREA";
static const char SC_S5[] PROGMEM = "KOMI";
static const char SC_S6[] PROGMEM = "TOTAL";
static const char SC_S7[] PROGMEM = "6.5";
static const char SC_S8[] PROGMEM = ".5";
static const char IN_S0[] PROGMEM = "CAPTURES\nB:\nW:";
static const char IN_S1[] PROGMEM = "A:PLACE  B:PASS";
static const char TT_S0[] PROGMEM = {4, 17, 7, 20, 9, 15, 0};  // "ARDUGO" (large-font glyph numbers)
static const char TT_S1[] PROGMEM = "PLAY VS AI\nPLAY VS HUMAN\nSHOW RULES\nINVERT SCREEN";
static const char HL_S0[] PROGMEM = {17, 20, 13, 8, 18, 0};  // "RULES" (large-font glyph numbers)
static const char HL_S1[] PROGMEM =
    "SURROUND TERRITORY WITH STONES.\n"
    "CAPTURE BY REMOVING LIBERTIES.\n"
    "NO SUICIDE. KO RULE APPLIES.\n"
    "PASS WHEN NO GOOD MOVES.\n"
    "TWO PASSES ENDS THE GAME.\n"
    "SCORE = TERRITORY + STONES.\n"
    "WHITE GETS 6.5 KOMI.";
static const ScLabel UI_LBLS[] PROGMEM = {
    // renderScoring [0,8)
    {66, 1, SC_S0}, {96, 11, SC_S1}, {116, 11, SC_S2},
    {66, 19, SC_S3}, {66, 31, SC_S5},
    {66, 39, SC_S6}, {96, 31, SC_S7},  // "6.5" at EW-10 = 96
    {100, 39, SC_S8},                  // ".5" at EW-6 = 100
    // renderInfo statics [8,10)
    {66, 12, IN_S0}, {66, 56, IN_S1},
    // renderTitle [10,12): ARDUGO is large print (bit 7)
    {46 | 0x80, 10, TT_S0}, {39, 28, TT_S1},
    // renderHelp [12,14)
    {49 | 0x80, 1, HL_S0}, {1, 15, HL_S1},
};
#define LBL_SCORING 0, 6   // S7 (komi) + S8 (.5) drawn dynamically now
#define LBL_INFO    8, 10
#define LBL_TITLE  10, 12
#define LBL_HELP   12, 14
static void drawLabels(Jaylib &jay, uint8_t from, uint8_t to) {
    for(uint8_t i = from; i < to; i++) {
        uint8_t x = pgm_read_byte(&UI_LBLS[i].x);
        uint8_t y = pgm_read_byte(&UI_LBLS[i].y);
        const __FlashStringHelper *s =
            (const __FlashStringHelper *)pgm_read_ptr(&UI_LBLS[i].s);
        if(x & 0x80) jay.largePrintPgm(x & 0x7F, y, s, 1);
        else jay.smallPrintPgm(x, y, s, 1);
    }
}

void Display::renderInfo() {
    uint8_t ix = 66; // clear of the board: stones/cursor reach x=63
    // Turn indicator
    jay.smallPrintPgm(ix, 4, game.turn == BLACK ? F("BLACK TO PLAY")
                                                : F("WHITE TO PLAY"), 1);

    // Captures block + controls hint (static labels; see UI_LBLS for
    // the coordinates and the layout comments that used to sit here)
    drawLabels(jay, LBL_INFO);
    jay.prNum(ix + 8, 18, game.captures[0]);
    uint8_t wx = jay.prNum(ix + 8, 24, game.captures[1]);
    // komi readout follows the game's actual komi (difficulty ladder)
    jay.smallPrintPgm(wx + 4, 24, F("+"), 1);
    uint8_t kx = jay.prNum(wx + 8, 24, game.kpieces / 2);
    jay.smallPrintPgm(kx, 24, F(".5"), 1);
    jay.drawFastHLine(ix, 31, 59);
    jay.drawFastHLine(ix, 52, 59);
}

void Display::renderTitle(uint8_t menuCursor) {
    drawLabels(jay, LBL_TITLE);
    jay.drawFastHLine(46, 18, 35);
    jay.drawFastHLine(50, 20, 27);

    jay.smallPrintPgm(
        30 + (jay.counter / 4) % 4,
        28 + menuCursor * 6, F(">"), 1
    );
}

// Difficulty ladder labels (see DIFFS in ardu_go.ino — order must match)
static const char DF_0[] PROGMEM = "25 KYU: BLACK, 3 HANDICAP";
static const char DF_1[] PROGMEM = "18 KYU: BLACK, 2 HANDICAP";
static const char DF_2[] PROGMEM = "13 KYU: BLACK, NO KOMI";
static const char DF_3[] PROGMEM = "11 KYU: EVEN";
static const char DF_4[] PROGMEM = "9 KYU: WHITE, NO KOMI";
static const char DF_5[] PROGMEM = "5 KYU: WHITE, 2 HANDICAP";
static const char DF_6[] PROGMEM = "1 KYU: WHITE, 3 HANDICAP";
static const char* const DF_LBLS[7] PROGMEM =
    { DF_0, DF_1, DF_2, DF_3, DF_4, DF_5, DF_6 };

void Display::renderDiffSel(uint8_t cursor) {
    jay.smallPrintPgm(34, 1, F("CHOOSE OPPONENT"), 1);
    jay.drawFastHLine(34, 8, 57);
    for(uint8_t i = 0; i < 7; i++)
        // single-digit ranks indent one full glyph (4 px) so the
        // colons align — a space only advances 2 px in this font
        jay.smallPrintPgm(i >= 4 ? 14 : 10, 12 + i * 7,
            (const __FlashStringHelper *)pgm_read_ptr(&DF_LBLS[i]), 1);
    // same animated cursor idiom as the title menu
    jay.smallPrintPgm(2 + (jay.counter / 4) % 4, 12 + cursor * 7, F(">"), 1);
}

void Display::renderHelp() {
    drawLabels(jay, LBL_HELP);
    jay.drawFastHLine(49, 9, 29);
}

// Right-aligned number in the score table: the small font advances
// 4 px/char (last glyph 3 px wide), so a d-digit value starts at
// edge - 4d + 1 to end flush at `edge`.
static void scoreNum(Jaylib &jay, uint8_t edge, uint8_t y, uint16_t v) {
    uint8_t d = v >= 100 ? 3 : v >= 10 ? 2 : 1;
    jay.prNum(edge - 4 * d + 1, y, v);
}

// Right-aligned cells: {edge, y}; values filled from the game at
// matching indices below.
static const uint8_t SC_CELLS[][2] PROGMEM = {
    {106, 19}, {126, 19}, {99, 39}, {126, 39},
};
void Display::renderScoring() {
    // Chinese/AREA: each side's living stones on the board + surrounded
    // territory (dead stones already removed by scoreDead). Komi (and the
    // .5 in white's total) lives only in the white column.
    drawLabels(jay, LBL_SCORING);
    jay.drawFastHLine(66, 37, 61);      // rule above the totals
    uint16_t bs = 0, ws = 0;
    for(uint8_t i = 0; i < BOARD_CELLS; i++) {
        uint8_t c = packedGet(game.board, i);
        if(c == BLACK) bs++;
        else if(c == WHITE) ws++;
    }
    uint16_t areaW = ws + game.territory[1];
    uint16_t areaB = bs + game.territory[0];
    // white total is always x.5 (area + 6.5 komi; integer 6 here, .5 via SC_S8)
    uint16_t vals[4] = { areaW, areaB,
                         (uint16_t)(areaW + game.kpieces / 2), areaB };
    // komi row + the .5 suffixes (were static labels; komi varies now)
    uint8_t kx = jay.prNum(96, 31, game.kpieces / 2);
    jay.smallPrintPgm(kx, 31, F(".5"), 1);
    jay.smallPrintPgm(100, 39, F(".5"), 1);
    for(uint8_t i = 0; i < 4; i++)
        scoreNum(jay, pgm_read_byte(&SC_CELLS[i][0]),
                 pgm_read_byte(&SC_CELLS[i][1]), vals[i]);
}

// Large-font prompts, pre-encoded as 1-based glyph numbers (space=1, !=2,
// then the letters per PRINTABLE_CHARS_LARGE). Trailing 0 terminates.
static const char GO_WRES[] PROGMEM = {21,10,11,19,8, 1, 17,8,18,11,9,14,18, 2, 0};  // "WHITE RESIGNS!"
static const char GO_BRES[] PROGMEM = { 5,13, 4,6,12, 1, 17,8,18,11,9,14,18, 2, 0};  // "BLACK RESIGNS!"
static const char GO_BWIN[] PROGMEM = { 5,13, 4,6,12, 1, 21,11,14,18, 2, 0};         // "BLACK WINS!"
static const char GO_WWIN[] PROGMEM = {21,10,11,19,8, 1, 21,11,14,18, 2, 0};         // "WHITE WINS!"
// The blocking MCTS search overwrites sBuffer with its node pool while
// the OLED keeps showing THIS frame, so it must be clean. chooseMove()/
// nnOpeningMove leaves opening scratch in the borrowed buffer, and prior
// tree wreckage may linger, so clear first. Encapsulated (not inline in
// the .ino) so scrprobe can assert it is dirty-buffer-independent.
void Display::renderThinkFrame() {
    memset(Arduboy2Base::sBuffer, 0, 1024);   // = jay.clear() on device
    renderBoard();
    renderInfo();
    jay.smallPrintPgm(66, 35, F("AI THINKING..."), 1);
}

void Display::renderGameOver() {
    if(game.resignedBy) {
        jay.drawPromptPgm(22, (const __FlashStringHelper *)
            (game.resignedBy == WHITE ? GO_WRES : GO_BRES), 0);
        return;
    }
    uint8_t w = game.areaWinner();
    jay.drawPromptPgm(22, (const __FlashStringHelper *)
        (w == BLACK ? GO_BWIN : GO_WWIN), 0);
}
