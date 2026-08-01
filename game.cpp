#include "game.h"

void Game::reset() {
    memset(board, EMPTY, sizeof(board));
    memset(prevBoard, EMPTY, sizeof(prevBoard));
    turn = BLACK;
    captures[0] = captures[1] = 0;
    consecutivePasses = 0;
    cursorX = cursorY = 4;
    kpieces = 13; // 6.5 komi
    territory[0] = territory[1] = 0;
    resignedBy = 0;
}

uint8_t Game::at(uint8_t x, uint8_t y) {
    return packedGet(board, y * BOARD_SIZE + x);
}

void Game::set(uint8_t x, uint8_t y, uint8_t val) {
    packedSet(board, y * BOARD_SIZE + x, val);
}

uint8_t floodScratch[BOARD_CELLS];

// Shared orthogonal offsets: four functions each materialized these
// as stack locals (init code + 8 stack bytes per instance).
static const int8_t DX4[] = {-1, 1, 0, 0};
static const int8_t DY4[] = {0, 0, -1, 1};

// Flood step shared by the four direction probes below (each was ~50
// unrolled bytes; the function is cold - real moves only).
__attribute__((noinline))
static uint8_t ffPush(Game *g, uint8_t p2, uint8_t color, uint8_t sp) {
    if(!packedGet(g->visited, p2) && packedGet(g->board, p2) == color) {
        packedSet(g->visited, p2, 1);
        floodSlot(sp++) = p2;
    }
    return sp;
}

uint8_t Game::floodFill(uint8_t x, uint8_t y, uint8_t color) {
    // Marks the connected region of `color` containing (x,y) in visited[]
    // and returns its size. Iterative: recursion would overflow the AVR
    // stack on large groups.
    if(x >= BOARD_SIZE || y >= BOARD_SIZE) return 0;
    uint8_t start = y * BOARD_SIZE + x;
    if(packedGet(visited, start) || packedGet(board, start) != color) return 0;

    uint8_t count = 0;
    uint8_t sp = 0;
    packedSet(visited, start, 1);
    floodSlot(sp++) = start;
    while(sp) {
        uint8_t p = floodSlot(--sp);
        count++;
        uint8_t px = p % BOARD_SIZE;
        uint8_t py = p / BOARD_SIZE;
        if(px > 0)              sp = ffPush(this, p - 1, color, sp);
        if(px < BOARD_SIZE - 1) sp = ffPush(this, p + 1, color, sp);
        if(py > 0)              sp = ffPush(this, p - BOARD_SIZE, color, sp);
        if(py < BOARD_SIZE - 1) sp = ffPush(this, p + BOARD_SIZE, color, sp);
    }
    return count;
}

// Neighbour d of cell j on the packed board, 0xFF when off-board.
// Shared by the two cold region scanners below (each inlined the
// %/÷ split + bounds + index math).
// Write `value` into dst at every cell the last flood visited.
// Shared by captureGroup, computeScore and the ko simulation.
__attribute__((noinline))
static void sweepVisited(Game *g, uint8_t *dst, uint8_t value) {
    for(uint8_t i = 0; i < BOARD_CELLS; i++)
        if(packedGet(g->visited, i)) packedSet(dst, i, value);
}

__attribute__((noinline))
static uint8_t nbIndexXY(uint8_t gx, uint8_t gy, uint8_t d) {
    int8_t nx = gx + DX4[d], ny = gy + DY4[d];
    if(nx < 0 || nx >= BOARD_SIZE || ny < 0 || ny >= BOARD_SIZE) return 0xFF;
    return (uint8_t)(ny * BOARD_SIZE + nx);
}
__attribute__((noinline))
static uint8_t nbIndex(uint8_t j, uint8_t d) {
    return nbIndexXY(j % BOARD_SIZE, j / BOARD_SIZE, d);
}

uint8_t Game::countLiberties(uint8_t x, uint8_t y) {
    uint8_t color = at(x, y);
    if(color == EMPTY) return 0;

    memset(visited, 0, sizeof(visited));
    floodFill(x, y, color); // group cells -> 1

    // Count each empty neighbor once by marking it 2 in visited[]
    // (group cells are stones, so the values never clash)
    uint8_t liberties = 0;
    for(uint8_t i = 0; i < BOARD_CELLS; i++) {
        if(packedGet(visited, i) != 1) continue;
        for(uint8_t d = 0; d < 4; d++) {
            uint8_t ni = nbIndex(i, d);
            if(ni == 0xFF) continue;
            if(packedGet(board, ni) == EMPTY && !packedGet(visited, ni)) {
                packedSet(visited, ni, 2);
                liberties++;
            }
        }
    }
    return liberties;
}

uint8_t Game::captureGroup(uint8_t x, uint8_t y) {
    uint8_t color = at(x, y);
    if(color == EMPTY) return 0;

    memset(visited, 0, sizeof(visited));
    uint8_t count = floodFill(x, y, color);

    sweepVisited(this, board, EMPTY);
    return count;
}

uint8_t Game::isValidMove(uint8_t x, uint8_t y) {
    if(x >= BOARD_SIZE || y >= BOARD_SIZE) return 0;
    if(at(x, y) != EMPTY) return 0;

    // Temporarily place stone
    uint8_t opponent = (turn == BLACK) ? WHITE : BLACK;
    set(x, y, turn);

    // Check if move captures opponent stones
    uint8_t captures = 0;
    for(uint8_t d = 0; d < 4; d++) {
        uint8_t ni = nbIndexXY(x, y, d);
        if(ni == 0xFF) continue;
        if(packedGet(board, ni) == opponent &&
           countLiberties(ni % BOARD_SIZE, ni / BOARD_SIZE) == 0)
            captures = 1;
    }

    // Check suicide: if no captures and own group has no liberties
    if(!captures && countLiberties(x, y) == 0) {
        set(x, y, EMPTY);
        return 0;
    }

    // Check ko: would this recreate previous board state?
    // Need to simulate the full move to check
    uint8_t tempBoard[PACKED_BOARD_BYTES];
    memcpy(tempBoard, board, sizeof(board));

    // Do captures on temp
    for(uint8_t d = 0; d < 4; d++) {
        uint8_t ni = nbIndexXY(x, y, d);
        if(ni == 0xFF) continue;
        if(packedGet(tempBoard, ni) == opponent) {
            // Check liberties in temp context — use board directly
            uint8_t nx = ni % BOARD_SIZE, ny = ni / BOARD_SIZE;
            if(countLiberties(nx, ny) == 0) {
                memset(visited, 0, sizeof(visited));
                floodFill(nx, ny, opponent);
                sweepVisited(this, tempBoard, EMPTY);
            }
        }
    }

    // Compare with previous board
    uint8_t isKo = (memcmp(tempBoard, prevBoard, sizeof(prevBoard)) == 0);

    set(x, y, EMPTY);
    return !isKo;
}

uint8_t Game::playMove(uint8_t x, uint8_t y) {
    if(!isValidMove(x, y)) return 0;

    memcpy(prevBoard, board, sizeof(board));
    set(x, y, turn);

    uint8_t opponent = (turn == BLACK) ? WHITE : BLACK;
    uint8_t captureIdx = turn == BLACK ? 0 : 1;

    for(uint8_t d = 0; d < 4; d++) {
        uint8_t ni = nbIndexXY(x, y, d);
        if(ni == 0xFF) continue;
        uint8_t nx = ni % BOARD_SIZE, ny = ni / BOARD_SIZE;
        if(at(nx, ny) == opponent && countLiberties(nx, ny) == 0) {
            captures[captureIdx] += captureGroup(nx, ny);
        }
    }

    consecutivePasses = 0;
    turn = opponent;
    return 1;
}

void Game::pass() {
    memcpy(prevBoard, board, sizeof(board));
    consecutivePasses++;
    turn = (turn == BLACK) ? WHITE : BLACK;
}

uint8_t Game::isGameOver() {
    return consecutivePasses >= 2;
}

void Game::computeScore() {
    territory[0] = territory[1] = 0;

    uint8_t owner[PACKED_BOARD_BYTES];
    memset(owner, EMPTY, sizeof(owner));

    for(uint8_t i = 0; i < BOARD_CELLS; i++) {
        if(packedGet(board, i) != EMPTY || packedGet(owner, i) != EMPTY)
            continue;

        // Flood fill empty region
        memset(visited, 0, sizeof(visited));
        floodFill(i % BOARD_SIZE, i / BOARD_SIZE, EMPTY);

        // Determine owner: check all borders of this region
        uint8_t touchesBlack = 0, touchesWhite = 0;
        uint8_t count = 0;
        for(uint8_t j = 0; j < BOARD_CELLS; j++) {
            if(!packedGet(visited, j)) continue;
            count++;
            for(uint8_t d = 0; d < 4; d++) {
                uint8_t ni = nbIndex(j, d);
                if(ni == 0xFF) continue;
                uint8_t nc = packedGet(board, ni);
                if(nc == BLACK) touchesBlack = 1;
                if(nc == WHITE) touchesWhite = 1;
            }
        }

        uint8_t regionOwner = EMPTY;
        if(touchesBlack && !touchesWhite) regionOwner = BLACK;
        if(touchesWhite && !touchesBlack) regionOwner = WHITE;

        sweepVisited(this, owner, regionOwner);

        if(regionOwner == BLACK) territory[0] += count;
        if(regionOwner == WHITE) territory[1] += count;
    }
}

uint8_t Game::winner() {
    if(resignedBy) return 3 - resignedBy;
    // Japanese scoring: territory + captures
    // Black score = territory[0] + captures[0]
    // White score = territory[1] + captures[1] + komi
    // kpieces is komi in half-points (13 = 6.5)
    uint16_t blackScore = territory[0] * 2 + captures[0] * 2;
    uint16_t whiteScore = territory[1] * 2 + captures[1] * 2 + kpieces;
    return (blackScore > whiteScore) ? BLACK : WHITE;
}
