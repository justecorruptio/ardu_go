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
        if(px > 0 && !packedGet(visited, p - 1) &&
           packedGet(board, p - 1) == color) {
            packedSet(visited, p - 1, 1);
            floodSlot(sp++) = p - 1;
        }
        if(px < BOARD_SIZE - 1 && !packedGet(visited, p + 1) &&
           packedGet(board, p + 1) == color) {
            packedSet(visited, p + 1, 1);
            floodSlot(sp++) = p + 1;
        }
        if(py > 0 && !packedGet(visited, p - BOARD_SIZE) &&
           packedGet(board, p - BOARD_SIZE) == color) {
            packedSet(visited, p - BOARD_SIZE, 1);
            floodSlot(sp++) = p - BOARD_SIZE;
        }
        if(py < BOARD_SIZE - 1 && !packedGet(visited, p + BOARD_SIZE) &&
           packedGet(board, p + BOARD_SIZE) == color) {
            packedSet(visited, p + BOARD_SIZE, 1);
            floodSlot(sp++) = p + BOARD_SIZE;
        }
    }
    return count;
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
        uint8_t gx = i % BOARD_SIZE, gy = i / BOARD_SIZE;
        const int8_t dx[] = {-1, 1, 0, 0};
        const int8_t dy[] = {0, 0, -1, 1};
        for(uint8_t d = 0; d < 4; d++) {
            int8_t nx = gx + dx[d], ny = gy + dy[d];
            if(nx < 0 || nx >= BOARD_SIZE || ny < 0 || ny >= BOARD_SIZE) continue;
            uint8_t ni = ny * BOARD_SIZE + nx;
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

    for(uint8_t i = 0; i < BOARD_CELLS; i++) {
        if(packedGet(visited, i)) packedSet(board, i, EMPTY);
    }
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
    const int8_t dx[] = {-1, 1, 0, 0};
    const int8_t dy[] = {0, 0, -1, 1};
    for(uint8_t d = 0; d < 4; d++) {
        int8_t nx = x + dx[d], ny = y + dy[d];
        if(nx < 0 || nx >= BOARD_SIZE || ny < 0 || ny >= BOARD_SIZE) continue;
        if(at(nx, ny) == opponent && countLiberties(nx, ny) == 0)
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
        int8_t nx = x + dx[d], ny = y + dy[d];
        if(nx < 0 || nx >= BOARD_SIZE || ny < 0 || ny >= BOARD_SIZE) continue;
        if(packedGet(tempBoard, ny * BOARD_SIZE + nx) == opponent) {
            // Check liberties in temp context — use board directly
            if(countLiberties(nx, ny) == 0) {
                memset(visited, 0, sizeof(visited));
                floodFill(nx, ny, opponent);
                for(uint8_t i = 0; i < BOARD_CELLS; i++) {
                    if(packedGet(visited, i)) packedSet(tempBoard, i, EMPTY);
                }
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

    const int8_t dx[] = {-1, 1, 0, 0};
    const int8_t dy[] = {0, 0, -1, 1};
    for(uint8_t d = 0; d < 4; d++) {
        int8_t nx = x + dx[d], ny = y + dy[d];
        if(nx < 0 || nx >= BOARD_SIZE || ny < 0 || ny >= BOARD_SIZE) continue;
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
            uint8_t gx = j % BOARD_SIZE, gy = j / BOARD_SIZE;
            const int8_t dx[] = {-1, 1, 0, 0};
            const int8_t dy[] = {0, 0, -1, 1};
            for(uint8_t d = 0; d < 4; d++) {
                int8_t nx = gx + dx[d], ny = gy + dy[d];
                if(nx < 0 || nx >= BOARD_SIZE || ny < 0 || ny >= BOARD_SIZE) continue;
                uint8_t nc = packedGet(board, ny * BOARD_SIZE + nx);
                if(nc == BLACK) touchesBlack = 1;
                if(nc == WHITE) touchesWhite = 1;
            }
        }

        uint8_t regionOwner = EMPTY;
        if(touchesBlack && !touchesWhite) regionOwner = BLACK;
        if(touchesWhite && !touchesBlack) regionOwner = WHITE;

        for(uint8_t j = 0; j < BOARD_CELLS; j++) {
            if(packedGet(visited, j)) packedSet(owner, j, regionOwner);
        }

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
