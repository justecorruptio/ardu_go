#include "ai.h"
#include "opening_book.h"
#include "neighbor_table.h"
#include "pattern_table.h"

// ==================== Opening book ====================

// The trie stores moves in book coordinates. A candidate symmetry s maps
// game coordinates -> book coordinates via applySym; its inverse maps a
// book move back onto the actual game.
// s: bit0 = flip y, bit1 = flip x, bit2 = transpose (flips first)
static void applySym(uint8_t &x, uint8_t &y, uint8_t s) {
    uint8_t t;
    if(s & 1) y = BOARD_SIZE - 1 - y;
    if(s & 2) x = BOARD_SIZE - 1 - x;
    if(s & 4) { t = x; x = y; y = t; }
}

static void applyInvSym(uint8_t &x, uint8_t &y, uint8_t s) {
    uint8_t t;
    if(s & 4) { t = x; x = y; y = t; }
    if(s & 1) y = BOARD_SIZE - 1 - y;
    if(s & 2) x = BOARD_SIZE - 1 - x;
}

void AI::reset() {
    bookAlive = 0xFF;
    for(uint8_t s = 0; s < 8; s++)
        bookPos[s] = 0;
    firstMove = 1;
}

// Skip the node at p and its whole subtree; returns offset just past it
uint16_t AI::trieSkip(uint16_t p) {
    uint8_t b = pgm_read_byte(OPENING_BOOK_TRIE + p);
    p++;
    if(!(b & 0x80)) { // internal: consume child list
        uint8_t last;
        do {
            last = pgm_read_byte(OPENING_BOOK_TRIE + p) & 0x40;
            p = trieSkip(p);
        } while(!last);
    }
    return p;
}

// Scan the sibling list starting at p for a node with the given move.
// Returns its offset, or -1 if not present.
int16_t AI::trieFindChild(uint16_t p, uint8_t moveIdx) {
    while(1) {
        uint8_t b = pgm_read_byte(OPENING_BOOK_TRIE + p);
        if((b & 0x3F) == moveIdx) return p;
        if(b & 0x40) return -1; // that was the last sibling
        p = trieSkip(p);
    }
}

void AI::notifyMove(uint8_t x, uint8_t y) {
    firstMove = 0;

    // Book never contains first-line moves
    if(x == 0 || x == BOARD_SIZE - 1 || y == 0 || y == BOARD_SIZE - 1) {
        bookAlive = 0;
        return;
    }

    for(uint8_t s = 0; s < 8; s++) {
        if(!(bookAlive & (1 << s))) continue;

        uint8_t bx = x, by = y;
        applySym(bx, by, s);
        int16_t q = trieFindChild(bookPos[s], (by - 1) * 7 + (bx - 1));
        if(q < 0) {
            bookAlive &= ~(1 << s);
            continue;
        }
        uint8_t b = pgm_read_byte(OPENING_BOOK_TRIE + q);
        if(b & 0x80) { // leaf: matched, but nothing stored beyond it
            bookAlive &= ~(1 << s);
            continue;
        }
        bookPos[s] = q + 1; // children start right after the node byte
    }
}

void AI::notifyPass() {
    firstMove = 0;
    bookAlive = 0; // a pass leaves all book lines
}

uint8_t AI::bookLookup(uint8_t &x, uint8_t &y) {
    if(firstMove) {
        // AI plays first: weighted pick among the root options,
        // in a random orientation for variety
        uint16_t total = 0;
        for(uint8_t i = 0; i < OPENING_BOOK_ROOT_OPTIONS; i++)
            total += pgm_read_byte(OPENING_ROOT_WEIGHTS + i);

        int16_t r = random(total);
        uint16_t p = 0;
        for(uint8_t i = 0; ; i++) {
            r -= pgm_read_byte(OPENING_ROOT_WEIGHTS + i);
            if(r < 0) break;
            p = trieSkip(p);
        }

        uint8_t idx = pgm_read_byte(OPENING_BOOK_TRIE + p) & 0x3F;
        x = idx % 7 + 1;
        y = idx / 7 + 1;
        applyInvSym(x, y, random(8));
        return 1;
    }

    if(!bookAlive) return 0;

    for(uint8_t s = 0; s < 8; s++) {
        if(!(bookAlive & (1 << s))) continue;
        // First child = highest-policy move
        uint8_t idx = pgm_read_byte(OPENING_BOOK_TRIE + bookPos[s]) & 0x3F;
        x = idx % 7 + 1;
        y = idx / 7 + 1;
        applyInvSym(x, y, s);
        return 1;
    }
    return 0;
}

uint8_t AI::chooseMove(Game &game) {
    uint8_t x, y;
    if(bookLookup(x, y) && game.isValidMove(x, y)) {
        game.playMove(x, y);
        notifyMove(x, y);
        return 1;
    }
    return 0;
}

// ==================== MCTS + UCB1 ====================

#define NODE_POOL_SB 143    // nodes in the borrowed screen buffer
#define NODE_POOL_EXT 33    // extension nodes in ordinary statics
#define NODE_POOL (NODE_POOL_SB + NODE_POOL_EXT) // must stay < 255 (8-bit links)
#define MCTS_ITERATIONS 600 // must stay under ~3400: 12-bit visit counters
#define RAVE_K 300          // Gelly-Silver beta schedule constant
#define LCB_Z 328           // Q8 (1.28 sigma): root move picked by LCB

// Virtual win/visit priors seeded at expansion; real playout results
// accumulate on top and wash these out.
#define PRIOR_BASE_V 2
#define PRIOR_BASE_W 1
#define PRIOR_CAPTURE 6     // captures an enemy group (its last liberty)
#define PRIOR_SAVE 4        // extends an own group out of atari
#define PRIOR_ATARI 2       // puts an enemy group into atari
#define PRIOR_CENTER_MAX 2  // + min(distance from board edge, this)
#define PRIOR_LOCAL 2       // adjacent or diagonal to the previous move
#define PRIOR_PATTERN 3     // matches a local 3x3 shape pattern
#define PRIOR_BIG 2         // big open point (far from every stone)

// Below this many stones, low lines eat virtual losses: almost never
// right early, normal territory moves later
#define EARLY_STONES 20
#define PRIOR_EDGE_PENALTY 5    // first line
#define PRIOR_LINE2_PENALTY 3   // second line

// Only the first moves of a playout feed RAVE: a point that gets
// filled successfully in the endgame of most playouts says nothing
// about playing it NOW, and those stats were drowning the priors.
#define RAVE_HORIZON 24

// Progressive widening (non-root): children allowed = 1 + visits/RATE
#define WIDEN_RATE 6
#define WIDEN_CAP 16
#define PLAYOUT_CAP 100
#define MOVE_PASS 81
#define NO_KO 0xFF
#define ILLEGAL 0xFE
#define POISONED 0xFF0      // visits sentinel for illegal tree moves
                            // (stats are 12-bit: max real count 4079)

struct Node {
    uint8_t move;        // 0-80 board index, MOVE_PASS
    uint8_t firstChild;  // pool index, 0xFF = none
    uint8_t nextSibling; // pool index, 0xFF = none
    // visits and wins packed 12+12 bits across 3 bytes:
    // s0 = v[7:0], s1 = v[11:8] | w[3:0]<<4, s2 = w[11:4].
    // Wins are from the perspective of the player who made 'move'.
    uint8_t s[3];
};

// The tree spans two regions, routed by index in node():
//   indices 0..142  -> the borrowed 1KB screen buffer (see below)
//   indices 143..175 -> poolExt in ordinary statics
// The buffer borrow works because the search is blocking: the OLED
// retains the last display()ed frame while we trash the buffer, and
// the next jay.clear() redraw wipes any residue.
// Buffer layout: 143 nodes * 6 = 858, then two 81-byte RAVE tables = 1020.
static Node * const pool = (Node *)Arduboy2Base::sBuffer;

// Root-only RAVE (AMAF): for each board point, how often the root
// player played it anywhere in a simulation, and how often those
// simulations were won. Pure statistics — never used as indices — so
// a 0x800 magic-key stomp here is harmless noise, no redirect needed.
static uint8_t * const raveV = Arduboy2Base::sBuffer + NODE_POOL_SB * sizeof(Node);
static uint8_t * const raveW = Arduboy2Base::sBuffer + NODE_POOL_SB * sizeof(Node) + BOARD_CELLS;

// Which points the root player touched in the current simulation
static uint8_t raveMask[11];

static inline void raveMark(uint8_t pos) {
    raveMask[pos >> 3] |= 1 << (pos & 7);
}

// Spare nodes backing the 0x800 magic-key redirect — kept OUTSIDE the
// screen buffer so the redirect targets themselves are safe.
static Node poolSpare[2];

// Static pool extension
static Node poolExt[NODE_POOL_EXT];

// All pool accesses go through here: a node overlapping RAM 0x800-0x801
// (hardware can overwrite those bytes) is redirected to a spare, so a
// stomped link byte can never send the search out of bounds. The two
// danger bytes can straddle two adjacent nodes; i's parity picks
// distinct spares for them.
static inline Node& node(uint8_t i) {
    Node *p = (i < NODE_POOL_SB) ? pool + i
                                 : poolExt + (i - NODE_POOL_SB);
    uint16_t a = (uint16_t)p;
    if(a <= 0x801 && a + sizeof(Node) > 0x800)
        return poolSpare[i & 1];
    return *p;
}

// Packed 12-bit stat accessors
static inline uint16_t nVisits(uint8_t i) {
    Node &n = node(i);
    return n.s[0] | ((uint16_t)(n.s[1] & 0x0F) << 8);
}

static inline uint16_t nWins(uint8_t i) {
    Node &n = node(i);
    return (n.s[1] >> 4) | ((uint16_t)n.s[2] << 4);
}

static inline void nSetStats(uint8_t i, uint16_t v, uint16_t w) {
    Node &n = node(i);
    n.s[0] = v;
    n.s[1] = ((v >> 8) & 0x0F) | ((w & 0x0F) << 4);
    n.s[2] = w >> 4;
}

static inline void nBump(uint8_t i, uint8_t win) {
    nSetStats(i, nVisits(i) + 1, nWins(i) + win);
}
static uint8_t poolUsed;
static uint8_t rootTurn;
static uint8_t rootKo;
static uint8_t rootLast; // opponent's actual last move (0xFF = none)
static uint8_t rootStones; // stones on the real board at think() time
static uint8_t simKomi;
static uint8_t simBoard[BOARD_CELLS];
static uint8_t simMark[BOARD_CELLS];   // epoch marks for flood fill
static uint8_t markEpoch;
static uint16_t rngState;
// flood work stack: shared floodScratch[] from game.cpp

static uint16_t rnd16() {
    rngState ^= rngState << 7;
    rngState ^= rngState >> 9;
    rngState ^= rngState << 8;
    return rngState;
}

static uint8_t rnd(uint8_t n) {
    return rnd16() % n;
}

static uint8_t neighbors(uint8_t pos, uint8_t *nb) {
    const uint8_t *e = NEIGHBOR_TABLE + pos * 5;
    uint8_t n = pgm_read_byte(e);
    for(uint8_t i = 0; i < n; i++)
        nb[i] = pgm_read_byte(e + 1 + i);
    return n;
}

static void newMark() {
    if(++markEpoch == 0) {
        memset(simMark, 0, sizeof(simMark));
        markEpoch = 1;
    }
}

// Iterative flood with early exit on first liberty found
static uint8_t hasLiberty(uint8_t start) {
    uint8_t color = simBoard[start];
    uint8_t nb[4];
    newMark();
    uint8_t sp = 0;
    floodSlot(sp++) = start;
    simMark[start] = markEpoch;
    while(sp) {
        uint8_t p = floodSlot(--sp);
        uint8_t n = neighbors(p, nb);
        for(uint8_t i = 0; i < n; i++) {
            uint8_t q = nb[i];
            if(simBoard[q] == EMPTY) return 1;
            if(simBoard[q] == color && simMark[q] != markEpoch) {
                simMark[q] = markEpoch;
                floodSlot(sp++) = q;
            }
        }
    }
    return 0;
}

// Remove a group, using the board itself as the visited marker
static uint8_t removeGroup(uint8_t start) {
    uint8_t color = simBoard[start];
    uint8_t nb[4];
    uint8_t count = 0;
    uint8_t sp = 0;
    floodSlot(sp++) = start;
    simBoard[start] = EMPTY;
    while(sp) {
        uint8_t p = floodSlot(--sp);
        count++;
        uint8_t n = neighbors(p, nb);
        for(uint8_t i = 0; i < n; i++) {
            if(simBoard[nb[i]] == color) {
                simBoard[nb[i]] = EMPTY;
                floodSlot(sp++) = nb[i];
            }
        }
    }
    return count;
}

// If the group at start has exactly one liberty, return it; else 0xFF
static uint8_t soleLiberty(uint8_t start) {
    uint8_t color = simBoard[start];
    uint8_t nb[4];
    newMark();
    uint8_t sp = 0, lib = 0xFF;
    floodSlot(sp++) = start;
    simMark[start] = markEpoch;
    while(sp) {
        uint8_t p = floodSlot(--sp);
        uint8_t n = neighbors(p, nb);
        for(uint8_t i = 0; i < n; i++) {
            uint8_t q = nb[i];
            if(simBoard[q] == EMPTY) {
                if(lib == 0xFF) lib = q;
                else if(lib != q) return 0xFF; // a second liberty
            } else if(simBoard[q] == color && simMark[q] != markEpoch) {
                simMark[q] = markEpoch;
                floodSlot(sp++) = q;
            }
        }
    }
    return lib;
}

// Find a group's distinct liberties, early-exiting at 3. Fills l1/l2
// with the first two (0xFF if fewer). Returns the count, 0-3.
static uint8_t groupLibsFind(uint8_t start, uint8_t *l1, uint8_t *l2) {
    uint8_t color = simBoard[start];
    uint8_t nb[4];
    newMark();
    uint8_t sp = 0;
    uint8_t lib1 = 0xFF, lib2 = 0xFF;
    uint8_t count = 0;
    floodSlot(sp++) = start;
    simMark[start] = markEpoch;
    while(sp && count < 3) {
        uint8_t p = floodSlot(--sp);
        uint8_t n = neighbors(p, nb);
        for(uint8_t i = 0; i < n; i++) {
            uint8_t q = nb[i];
            if(simBoard[q] == EMPTY) {
                if(q != lib1 && q != lib2) {
                    if(lib1 == 0xFF) lib1 = q;
                    else if(lib2 == 0xFF) lib2 = q;
                    count++;
                    if(count >= 3) break;
                }
            } else if(simBoard[q] == color && simMark[q] != markEpoch) {
                simMark[q] = markEpoch;
                floodSlot(sp++) = q;
            }
        }
    }
    *l1 = lib1;
    *l2 = lib2;
    return count;
}

static uint8_t groupLibsMax3(uint8_t start) {
    uint8_t a, b;
    uint8_t n = groupLibsFind(start, &a, &b);
    return n ? n : 1; // preserve old behavior: 0 liberties reads as 1
}

// All orthogonal neighbors own color (or edge)
static uint8_t isOwnEye(uint8_t pos, uint8_t color) {
    uint8_t nb[4];
    uint8_t n = neighbors(pos, nb);
    for(uint8_t i = 0; i < n; i++)
        if(simBoard[nb[i]] != color) return 0;
    return 1;
}

// Play on simBoard. Returns new ko point, NO_KO, or ILLEGAL.
// noSelfAtari additionally rejects non-capturing moves that leave the
// placed group at one liberty (the playout policy). Cheap by
// construction: when nothing was captured the only mutation is the
// placed stone, so rejection is a one-byte undo — no snapshot needed.
static uint8_t simPlay(uint8_t pos, uint8_t color, uint8_t ko,
                       uint8_t noSelfAtari = 0) {
    if(pos == MOVE_PASS) return NO_KO;
    if(pos == ko || simBoard[pos] != EMPTY) return ILLEGAL;

    uint8_t opp = 3 - color;
    uint8_t nb[4];
    simBoard[pos] = color;

    uint8_t captured = 0, capPos = 0;
    uint8_t n = neighbors(pos, nb);
    for(uint8_t i = 0; i < n; i++) {
        if(simBoard[nb[i]] == opp && !hasLiberty(nb[i])) {
            capPos = nb[i];
            captured += removeGroup(nb[i]);
        }
    }

    if(!captured) {
        if(noSelfAtari) {
            // One flood covers both suicide (0 libs) and self-atari (1)
            uint8_t a, b;
            if(groupLibsFind(pos, &a, &b) < 2) {
                simBoard[pos] = EMPTY;
                return ILLEGAL;
            }
        } else if(!hasLiberty(pos)) { // suicide
            simBoard[pos] = EMPTY;
            return ILLEGAL;
        }
    }

    // Simple ko: lone stone with one liberty that captured exactly one
    if(captured == 1) {
        uint8_t lone = 1, libs = 0;
        for(uint8_t i = 0; i < n; i++) {
            if(simBoard[nb[i]] == color) lone = 0;
            else if(simBoard[nb[i]] == EMPTY) libs++;
        }
        if(lone && libs == 1) return capPos;
    }
    return NO_KO;
}

// Snapshot for ladder reading (static: too big for the AVR stack at
// the depths this gets called from)
static uint8_t ladderBoard[BOARD_CELLS];

// Light ladder reader. The group containing defStart is in atari with
// sole liberty esc; run the forced chase and return 1 if it escapes.
// Every ambiguity (depth cap, odd shapes, no working chase) resolves
// toward "escape", so a wrong read just reproduces the old behavior.
// Ko is ignored while reading. NOT reentrant (single snapshot).
static uint8_t ladderEscapes(uint8_t defStart, uint8_t esc) {
    memcpy(ladderBoard, simBoard, BOARD_CELLS);
    uint8_t defColor = simBoard[defStart];
    uint8_t atkColor = 3 - defColor;
    uint8_t escaped = 1;

    for(uint8_t step = 0; step < 20; step++) {
        // Defender extends at the sole liberty
        if(simPlay(esc, defColor, NO_KO) == ILLEGAL) {
            escaped = 0;
            break;
        }
        uint8_t l1, l2;
        uint8_t libs = groupLibsFind(esc, &l1, &l2);
        if(libs >= 3) break;               // clear escape
        if(libs <= 1) { escaped = 0; break; } // attacker just takes

        // Attacker chases at whichever liberty keeps his stone alive,
        // preferring the tighter side (less room around the defender's
        // remaining liberty)
        uint8_t chase = 0xFF, next = 0xFF;
        uint8_t bestRoom = 0xFF;
        for(uint8_t t = 0; t < 2; t++) {
            uint8_t cand = t ? l2 : l1;
            uint8_t other = t ? l1 : l2;
            simBoard[cand] = atkColor; // tentative, no captures
            uint8_t alibs = groupLibsMax3(cand);
            simBoard[cand] = EMPTY;
            if(alibs < 2) continue; // defender would just capture it

            uint8_t nb[4];
            uint8_t room = 0;
            uint8_t n = neighbors(other, nb);
            for(uint8_t i = 0; i < n; i++)
                if(simBoard[nb[i]] == EMPTY) room++;
            if(room < bestRoom) {
                bestRoom = room;
                chase = cand;
                next = other;
            }
        }
        if(chase == 0xFF) break; // no working chase: escape
        if(simPlay(chase, atkColor, NO_KO) == ILLEGAL) break;
        esc = next;
    }

    memcpy(simBoard, ladderBoard, BOARD_CELLS);
    return escaped;
}

// Does the 3x3 neighborhood of (cx,cy) match the MoGo pattern library
// for `color` to move? The library is precompiled into truth-table
// bitmaps per position class, so a query is a base-3 index over the
// on-board neighbors (colors relative to the mover; the set is
// color-swap closed, so this is exact) plus one bit test.
static uint8_t patternMatch(int8_t cx, int8_t cy, uint8_t color) {
    uint8_t clsx = (cx == 0) ? 0 : (cx == BOARD_SIZE - 1) ? 2 : 1;
    uint8_t clsy = (cy == 0) ? 0 : (cy == BOARD_SIZE - 1) ? 2 : 1;

    uint16_t idx = 0;
    uint16_t mult = 1;
    uint8_t stones = 0;
    for(int8_t dy = -1; dy <= 1; dy++) {
        for(int8_t dx = -1; dx <= 1; dx++) {
            if(dx == 0 && dy == 0) continue;
            int8_t x = cx + dx, y = cy + dy;
            if(x < 0 || x >= BOARD_SIZE || y < 0 || y >= BOARD_SIZE)
                continue; // off-board cells are implied by the class
            uint8_t s = simBoard[y * BOARD_SIZE + x];
            uint8_t v = (s == EMPTY) ? 0 : (s == color) ? 1 : 2;
            if(v) stones++;
            idx += v * mult;
            mult *= 3;
        }
    }
    if(stones < 2) return 0; // no pattern has fewer than two stones

    uint16_t off = pgm_read_word(PAT3_OFFSET + clsy * 3 + clsx);
    return (pgm_read_byte(PAT3_BITS + off + (idx >> 3)) >> (idx & 7)) & 1;
}

// Area scoring; returns winning color
static uint8_t scoreWinner() {
    uint8_t black = 0, white = 0;
    uint8_t nb[4];
    for(uint8_t i = 0; i < BOARD_CELLS; i++) {
        if(simBoard[i] == BLACK) { black++; continue; }
        if(simBoard[i] == WHITE) { white++; continue; }
        uint8_t tb = 0, tw = 0;
        uint8_t n = neighbors(i, nb);
        for(uint8_t j = 0; j < n; j++) {
            if(simBoard[nb[j]] == BLACK) tb = 1;
            if(simBoard[nb[j]] == WHITE) tw = 1;
        }
        if(tb && !tw) black++;
        else if(tw && !tb) white++;
    }
    return ((uint16_t)black * 2 > (uint16_t)white * 2 + simKomi) ? BLACK : WHITE;
}

// Random playout from current simBoard; returns winning color.
// `last` = the previous move (0xFF/pass = none).
static uint8_t playout(uint8_t toMove, uint8_t ko, uint8_t last) {
    uint8_t passes = 0;
    for(uint8_t m = 0; m < PLAYOUT_CAP && passes < 2; m++) {
        // Tactical: if the opponent's just-moved group is in atari,
        // capture it. Uniform-random playouts ignore atari, which
        // wrecks every life-and-death estimate.
        // Heuristics fire probabilistically (michi-style): applied
        // every time, all playouts make the SAME systematic errors and
        // those don't average out. Tactical ~7/8, patterns ~15/16.
        if(last < BOARD_CELLS && simBoard[last] != EMPTY && (rnd16() & 7)) {
            uint8_t tac = 0xFF, isSave = 0;

            // Capture the opponent's just-moved group if it is in atari
            uint8_t lib = soleLiberty(last);
            if(lib != 0xFF && lib != ko) {
                tac = lib;
            } else {
                // Else escape: their move may have put an own group
                // next to it into atari — extend at its last liberty
                uint8_t nb[4];
                uint8_t n = neighbors(last, nb);
                for(uint8_t i = 0; i < n; i++) {
                    if(simBoard[nb[i]] != toMove) continue;
                    uint8_t sl = soleLiberty(nb[i]);
                    if(sl != 0xFF && sl != ko && ladderEscapes(nb[i], sl)) {
                        tac = sl;
                        isSave = 1;
                        break;
                    }
                }
            }

            if(tac != 0xFF) {
                // Captures go direct (snapback reading allowed); a save
                // that self-ataris is a refused ladder step
                uint8_t nk = simPlay(tac, toMove, ko, isSave);
                if(nk != ILLEGAL) {
                    if(toMove == rootTurn && m < RAVE_HORIZON) raveMark(tac);
                    ko = nk;
                    last = tac;
                    passes = 0;
                    toMove = 3 - toMove;
                    continue;
                }
            }
        }

        // MoGo-style: 3x3 patterns at the 8 points around the last move
        if(last < BOARD_CELLS && (rnd16() & 15)) {
            int8_t lpx = last % BOARD_SIZE, lpy = last / BOARD_SIZE;
            uint8_t matches[8];
            uint8_t nMatches = 0;
            for(int8_t dy = -1; dy <= 1; dy++) {
                for(int8_t dx = -1; dx <= 1; dx++) {
                    if(dx == 0 && dy == 0) continue;
                    int8_t cx = lpx + dx, cy = lpy + dy;
                    if(cx < 0 || cx >= BOARD_SIZE || cy < 0 || cy >= BOARD_SIZE)
                        continue;
                    uint8_t pos = cy * BOARD_SIZE + cx;
                    if(simBoard[pos] != EMPTY || pos == ko) continue;
                    if(isOwnEye(pos, toMove)) continue;
                    if(patternMatch(cx, cy, toMove))
                        matches[nMatches++] = pos;
                }
            }
            if(nMatches) {
                uint8_t pos = matches[rnd(nMatches)];
                uint8_t nk = simPlay(pos, toMove, ko, 1);
                if(nk != ILLEGAL) {
                    if(toMove == rootTurn && m < RAVE_HORIZON) raveMark(pos);
                    ko = nk;
                    last = pos;
                    passes = 0;
                    toMove = 3 - toMove;
                    continue;
                }
            }
        }

        uint8_t start = rnd(BOARD_CELLS);
        uint8_t played = 0;
        for(uint8_t i = 0; i < BOARD_CELLS; i++) {
            uint8_t pos = start + i;
            if(pos >= BOARD_CELLS) pos -= BOARD_CELLS;
            if(simBoard[pos] != EMPTY || pos == ko) continue;

            // Lonely first-line moves are pure noise: skip unless the
            // point touches a stone. Neighbor count < 4 <=> first line.
            uint8_t nn = pgm_read_byte(NEIGHBOR_TABLE + pos * 5);
            if(nn < 4) {
                uint8_t contact = 0;
                for(uint8_t j = 1; j <= nn; j++) {
                    if(simBoard[pgm_read_byte(NEIGHBOR_TABLE + pos * 5 + j)] != EMPTY) {
                        contact = 1;
                        break;
                    }
                }
                if(!contact) continue;
            }

            if(isOwnEye(pos, toMove)) continue;
            uint8_t nk = simPlay(pos, toMove, ko, 1);
            if(nk == ILLEGAL) continue;
            if(toMove == rootTurn && m < RAVE_HORIZON) raveMark(pos);
            ko = nk;
            last = pos;
            played = 1;
            passes = 0;
            break;
        }
        if(!played) {
            passes++;
            last = 0xFF;
            ko = NO_KO;
        }
        toMove = 3 - toMove;
    }
    return scoreWinner();
}

static uint8_t newNode(uint8_t move) {
    Node &n = node(poolUsed);
    n.move = move;
    n.firstChild = 0xFF;
    n.nextSibling = 0xFF;
    n.s[0] = n.s[1] = n.s[2] = 0;
    return poolUsed++;
}

// 81-bit bitmap of points within distance 2 of any stone.
// Returns 0 if the board has no stones (then allow everything).
static uint8_t buildNearMask(uint8_t *near) {
    memset(near, 0, 11);
    uint8_t anyStone = 0;
    for(uint8_t p = 0; p < BOARD_CELLS; p++) {
        if(simBoard[p] == EMPTY) continue;
        anyStone = 1;
        uint8_t sx = p % BOARD_SIZE, sy = p / BOARD_SIZE;
        uint8_t x0 = sx > 2 ? sx - 2 : 0, x1 = sx < 6 ? sx + 2 : 8;
        uint8_t y0 = sy > 2 ? sy - 2 : 0, y1 = sy < 6 ? sy + 2 : 8;
        for(uint8_t yy = y0; yy <= y1; yy++)
            for(uint8_t xx = x0; xx <= x1; xx++) {
                uint8_t q = yy * BOARD_SIZE + xx;
                near[q >> 3] |= 1 << (q & 7);
            }
    }
    return anyStone;
}

// Prior for one candidate: tactics + center + locality + shape, minus
// early-game low-line penalties. Negative = virtual losses. isFar
// marks a big open point, which can never earn tactical or local
// credit and gets its own bonus instead.
static int8_t candidatePrior(uint8_t pos, uint8_t toMove, uint8_t last,
                             uint8_t isFar) {
    uint8_t opp = 3 - toMove;
    uint8_t sawCapture = 0, sawSave = 0, sawAtari = 0;
    uint8_t nb[4];
    uint8_t n = neighbors(pos, nb);
    for(uint8_t j = 0; j < n; j++) {
        uint8_t q = nb[j];
        if(simBoard[q] == opp) {
            uint8_t l = groupLibsMax3(q);
            if(l == 1) sawCapture = 1;      // pos is its last liberty
            else if(l == 2) sawAtari = 1;
        } else if(simBoard[q] == toMove) {
            // Only credit a save if the ladder actually works —
            // extending a ladder-dead group just feeds stones
            if(groupLibsMax3(q) == 1 && ladderEscapes(q, pos))
                sawSave = 1;
        }
    }

    int8_t bonus = sawCapture ? PRIOR_CAPTURE :
                   sawSave    ? PRIOR_SAVE :
                   sawAtari   ? PRIOR_ATARI : 0;

    // Center preference: the edge is worth less than the third line
    uint8_t x = pos % BOARD_SIZE, y = pos / BOARD_SIZE;
    uint8_t ex = x < BOARD_SIZE - 1 - x ? x : BOARD_SIZE - 1 - x;
    uint8_t ey = y < BOARD_SIZE - 1 - y ? y : BOARD_SIZE - 1 - y;
    uint8_t ed = ex < ey ? ex : ey;
    bonus += ed > PRIOR_CENTER_MAX ? PRIOR_CENTER_MAX : ed;

    // Locality: adjacent or diagonal to the previous move
    if(last < BOARD_CELLS) {
        int8_t dx = x - last % BOARD_SIZE; if(dx < 0) dx = -dx;
        int8_t dy = y - last / BOARD_SIZE; if(dy < 0) dy = -dy;
        if(dx <= 1 && dy <= 1) bonus += PRIOR_LOCAL;
    }

    // Local shape: the same 3x3 patterns the playouts use. This is what
    // makes cut-defense (blocking a keima push, connecting a jump)
    // visible to the tree instead of only to the rollouts.
    if(patternMatch(x, y, toMove)) bonus += PRIOR_PATTERN;

    // Big open point: the territory-staking move
    if(isFar) bonus += PRIOR_BIG;

    // Early game: penalize the low lines. Real tactics (capture/save)
    // still outweigh this; quiet edge crawls sink well below neutral.
    if(rootStones < EARLY_STONES) {
        if(ed == 0) bonus -= PRIOR_EDGE_PENALTY;
        else if(ed == 1) bonus -= PRIOR_LINE2_PENALTY;
    }

    return bonus;
}

// Push-front: child order carries no meaning beyond tie-breaking, and
// random scan starts keep the ties fair. A negative bonus becomes
// virtual losses: extra visits with no wins.
static uint8_t addChild(uint8_t nodeIdx, uint8_t move, int8_t bonus) {
    uint8_t c = newNode(move);
    if(bonus >= 0)
        nSetStats(c, PRIOR_BASE_V + bonus, PRIOR_BASE_W + bonus);
    else
        nSetStats(c, PRIOR_BASE_V - bonus, PRIOR_BASE_W);
    node(c).nextSibling = node(nodeIdx).firstChild;
    node(nodeIdx).firstChild = c;
    return c;
}

static uint8_t childCount(uint8_t nodeIdx) {
    uint8_t n = 0;
    for(uint8_t c = node(nodeIdx).firstChild; c != 0xFF; c = node(c).nextSibling)
        n++;
    return n;
}

// Root expansion: ALL pruned candidates at once — a move never created
// can never be chosen, so the root must cover everything. Non-root
// nodes grow one child at a time via widenNode instead.
static void expandNode(uint8_t nodeIdx, uint8_t toMove, uint8_t ko, uint8_t last) {
    uint8_t near[11];
    uint8_t anyStone = buildNearMask(near);

    if(poolUsed < NODE_POOL) {
        uint8_t c = addChild(nodeIdx, MOVE_PASS, 0);
        nSetStats(c, PRIOR_BASE_V, 0); // passing is a last resort
    }

    uint8_t startPos = rnd(BOARD_CELLS);
    for(uint8_t i = 0; i < BOARD_CELLS && poolUsed < NODE_POOL; i++) {
        uint8_t pos = startPos + i;
        if(pos >= BOARD_CELLS) pos -= BOARD_CELLS;
        if(simBoard[pos] != EMPTY || pos == ko) continue;
        uint8_t isFar = 0;
        if(anyStone && !(near[pos >> 3] & (1 << (pos & 7)))) {
            // Far from every stone: admit as a big point if it is on
            // the third line or higher — otherwise territory-staking
            // moves are unreachable and their RAVE evidence is wasted
            uint8_t bx = pos % BOARD_SIZE, by = pos / BOARD_SIZE;
            uint8_t bex = bx < BOARD_SIZE - 1 - bx ? bx : BOARD_SIZE - 1 - bx;
            uint8_t bey = by < BOARD_SIZE - 1 - by ? by : BOARD_SIZE - 1 - by;
            if((bex < bey ? bex : bey) < 2) continue;
            isFar = 1;
        }
        if(isOwnEye(pos, toMove)) continue;
        addChild(nodeIdx, pos, candidatePrior(pos, toMove, last, isFar));
    }
}

// Progressive widening: add the single best not-yet-present candidate.
// Poisoned children stay in the have-bitmap, so an illegal move is
// never re-added. Returns 1 if a child was added.
static uint8_t widenNode(uint8_t nodeIdx, uint8_t toMove, uint8_t ko, uint8_t last) {
    uint8_t have[11];
    memset(have, 0, sizeof(have));
    for(uint8_t c = node(nodeIdx).firstChild; c != 0xFF; c = node(c).nextSibling)
        if(node(c).move < BOARD_CELLS)
            have[node(c).move >> 3] |= 1 << (node(c).move & 7);

    uint8_t near[11];
    uint8_t anyStone = buildNearMask(near);

    int8_t bestP = -128;
    uint8_t bestPos = 0xFF;
    uint8_t startPos = rnd(BOARD_CELLS);
    for(uint8_t i = 0; i < BOARD_CELLS; i++) {
        uint8_t pos = startPos + i;
        if(pos >= BOARD_CELLS) pos -= BOARD_CELLS;
        if(simBoard[pos] != EMPTY || pos == ko) continue;
        if(have[pos >> 3] & (1 << (pos & 7))) continue;
        uint8_t isFar = 0;
        if(anyStone && !(near[pos >> 3] & (1 << (pos & 7)))) {
            uint8_t bx = pos % BOARD_SIZE, by = pos / BOARD_SIZE;
            uint8_t bex = bx < BOARD_SIZE - 1 - bx ? bx : BOARD_SIZE - 1 - bx;
            uint8_t bey = by < BOARD_SIZE - 1 - by ? by : BOARD_SIZE - 1 - by;
            if((bex < bey ? bex : bey) < 2) continue;
            isFar = 1;
        }
        if(isOwnEye(pos, toMove)) continue;
        int8_t p = candidatePrior(pos, toMove, last, isFar);
        if(p > bestP) {
            bestP = p;
            bestPos = pos;
        }
    }
    if(bestPos == 0xFF) return 0;
    addChild(nodeIdx, bestPos, bestP);
    return 1;
}

// Bitwise integer sqrt: isqrt32(x*2^24)/4096 approximates sqrt(x)
static uint16_t isqrt32(uint32_t x) {
    uint32_t res = 0;
    uint32_t bit = 1UL << 30;
    while(bit > x) bit >>= 2;
    while(bit) {
        if(x >= res + bit) {
            x -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return res;
}

// Q12 fractional part of log2(1 + i/16)
PROGMEM const uint16_t LOG2_FRAC[16] = {
    0, 358, 696, 1016, 1319, 1607, 1882, 2145,
    2396, 2637, 2869, 3092, 3307, 3514, 3715, 3908
};

// Q12 natural log for x >= 1: integer log2 (bit position + 4-bit
// mantissa table), scaled by ln2 (2839 in Q12)
static uint16_t lnQ12(uint16_t x) {
    if(x < 2) return 0;
    uint8_t k = 0;
    uint16_t t = x;
    while(t >>= 1) k++;
    uint8_t frac = (k >= 4) ? (x >> (k - 4)) & 0x0F
                            : (x << (4 - k)) & 0x0F;
    uint16_t log2q = ((uint16_t)k << 12) | pgm_read_word(LOG2_FRAC + frac);
    return (uint32_t)log2q * 2839 >> 12;
}

static uint8_t selectChild(uint8_t nodeIdx) {
    // UCB1-Tuned in Q12 fixed point — software floats cost several ms
    // per root scan, so everything here is integer.
    // +1: the parent may have no real visits yet on its first selection.
    uint16_t lnN = lnQ12(nVisits(nodeIdx) + 1);

    // At the root, blend in RAVE by the Gelly-Silver schedule: full
    // trust in AMAF early, fading toward real values as visits grow.
    uint16_t beta = 0; // Q12
    if(nodeIdx == 0) {
        uint16_t ratio = ((uint32_t)RAVE_K << 12) /
                         (3 * nVisits(0) + RAVE_K);
        beta = isqrt32((uint32_t)ratio << 12);
    }

    uint16_t best = 0;
    uint8_t bestC = node(nodeIdx).firstChild;
    for(uint8_t c = node(nodeIdx).firstChild; c != 0xFF; c = node(c).nextSibling) {
        Node &n = node(c);
        uint16_t nv = nVisits(c);
        uint16_t q = ((uint32_t)nWins(c) << 12) / nv;

        // Variance-aware exploration from the raw win rate: with
        // binary rewards the sample variance is just q(1-q).
        // Q12*Q12 products are Q24, so isqrt lands back in Q12.
        uint16_t lnOverN = lnN / nv;
        uint32_t v = ((uint32_t)q * (4096 - q)) >> 12;
        v += isqrt32((uint32_t)(2 * lnOverN) << 12);
        if(v > 1024) v = 1024; // min(1/4, ...)

        // Never lift a poisoned (illegal) child back up via RAVE
        if(beta && nv < POISONED &&
           n.move < BOARD_CELLS && raveV[n.move]) {
            uint16_t qr = ((uint32_t)raveW[n.move] << 12) / raveV[n.move];
            q = ((uint32_t)(4096 - beta) * q + (uint32_t)beta * qr) >> 12;
        }

        uint16_t u = q + isqrt32((uint32_t)lnOverN * v);
        if(u > best) {
            best = u;
            bestC = c;
        }
    }
    return bestC;
}

static void mctsIterate(Game &game) {
    // Unpack the 2-bit game board into the byte-per-cell sim board
    for(uint8_t i = 0; i < BOARD_CELLS; i++)
        simBoard[i] = packedGet(game.board, i);
    memset(raveMask, 0, sizeof(raveMask));
    uint8_t ko = rootKo;
    uint8_t toMove = rootTurn;
    uint8_t lastMove = rootLast;

    uint8_t path[32];
    uint8_t depth = 0;
    uint8_t cur = 0;
    path[depth++] = 0;
    uint8_t retries = 0;

    // Selection + expansion
    while(depth < 31) {
        if(node(cur).firstChild == 0xFF) {
            if(poolUsed < NODE_POOL) {
                if(cur == 0) expandNode(cur, toMove, ko, lastMove);
                else widenNode(cur, toMove, ko, lastMove);
            }
            if(node(cur).firstChild == 0xFF) break; // terminal or pool full
        } else if(cur != 0 && poolUsed < NODE_POOL) {
            // Progressive widening: one more candidate as visits grow
            uint8_t maxKids = 1 + nVisits(cur) / WIDEN_RATE;
            if(maxKids > WIDEN_CAP) maxKids = WIDEN_CAP;
            if(childCount(cur) < maxKids)
                widenNode(cur, toMove, ko, lastMove);
        }
        uint8_t c = selectChild(cur);
        uint8_t nk = simPlay(node(c).move, toMove, ko);
        if(nk == ILLEGAL) {
            nSetStats(c, POISONED, 0);
            if(++retries >= 4) break;
            continue;
        }
        if(toMove == rootTurn && node(c).move < BOARD_CELLS)
            raveMark(node(c).move);
        ko = nk;
        toMove = 3 - toMove;
        lastMove = node(c).move;
        cur = c;
        path[depth++] = c;
    }

    uint8_t winner = playout(toMove, ko, lastMove);

    // Fold this simulation into the root RAVE tables
    uint8_t rootWin = (winner == rootTurn);
    for(uint8_t i = 0; i < BOARD_CELLS; i++) {
        if(!(raveMask[i >> 3] & (1 << (i & 7)))) continue;
        if(raveV[i] == 255) { // saturate by halving, keeps the ratio
            raveV[i] >>= 1;
            raveW[i] >>= 1;
        }
        raveV[i]++;
        if(rootWin) raveW[i]++;
    }

    // Backprop. path[1] was played by rootTurn, path[2] by the opponent, ...
    for(uint8_t i = 0; i < depth; i++) {
        uint8_t win = 0;
        if(i > 0) {
            uint8_t mover = (i & 1) ? rootTurn : 3 - rootTurn;
            win = (mover == winner);
        }
        nBump(path[i], win);
    }
}

void AI::think(Game &game) {
    poolUsed = 0;
    rootTurn = game.turn;
    simKomi = game.kpieces;
    rngState = random(0xFFFF) | 1;

    // Real ko point: if the last move captured exactly one of our
    // stones, forbid the immediate recapture in search. Slightly
    // over-strict for multi-stone situations, but bestMove's validation
    // against the real rules has the final say.
    rootKo = NO_KO;
    rootStones = 0;
    rootLast = 0xFF;
    uint8_t caps = 0, capPos = 0;
    for(uint8_t i = 0; i < BOARD_CELLS; i++) {
        uint8_t cur = packedGet(game.board, i);
        uint8_t prev = packedGet(game.prevBoard, i);
        if(cur != EMPTY) rootStones++;
        if(prev == game.turn && cur == EMPTY) {
            caps++;
            capPos = i;
        }
        // Opponent's actual last move (their new stone vs prevBoard),
        // for the locality prior at the root. 0xFF if they passed.
        if(cur == 3 - game.turn && prev == EMPTY)
            rootLast = i;
    }
    if(caps == 1) rootKo = capPos;

    memset(raveV, 0, BOARD_CELLS);
    memset(raveW, 0, BOARD_CELLS);

    newNode(0xFF); // root
    for(uint16_t i = 0; i < MCTS_ITERATIONS; i++) {
        mctsIterate(game);

        // Early stop when the visit leader's margin exceeds the
        // remaining budget. With LCB move selection this is an
        // approximation rather than exact: a dominant visit lead
        // almost always implies a dominant LCB, but not provably.
        if((i & 31) == 31) {
            uint16_t top1 = 0, top2 = 0;
            for(uint8_t c = node(0).firstChild; c != 0xFF; c = node(c).nextSibling) {
                uint16_t v = nVisits(c);
                if(v >= POISONED) continue;
                if(v > top1) { top2 = top1; top1 = v; }
                else if(v > top2) top2 = v;
            }
            if(top1 - top2 > MCTS_ITERATIONS - 1 - i) break;
        }
    }
}

uint8_t AI::bestMove(Game &game, uint8_t &x, uint8_t &y) {
    // Root move by highest LOWER confidence bound on the win rate
    // (Leela-style): lcb = q - z*sqrt(q(1-q)/n), Q12 fixed point.
    // Beats most-visited when a visit-leader's win rate is decaying.
    // Low-visit children punish themselves via the wide bound, and the
    // real-rules validation (full ko) stays lazy: an invalid favorite
    // falls through to the next-best instead of turning into a pass.
    int16_t bestL = -32768;
    uint16_t backV = 0;
    uint8_t best = MOVE_PASS, backup = MOVE_PASS;
    for(uint8_t c = node(0).firstChild; c != 0xFF; c = node(c).nextSibling) {
        uint16_t v = nVisits(c);
        if(v >= POISONED) continue;
        uint8_t m = node(c).move;

        // Fallback: most-visited, in case no child clears the LCB gate
        if(v > backV) {
            if(m == MOVE_PASS ||
               game.isValidMove(m % BOARD_SIZE, m / BOARD_SIZE)) {
                backV = v;
                backup = m;
            }
        }

        // Gate: prior-seeded children carry inflated q at tiny n and
        // would fake a strong LCB — demand real sampling first
        if(v < 24) continue;

        uint16_t q = ((uint32_t)nWins(c) << 12) / v;
        uint32_t var = ((uint32_t)q * (4096 - q)) >> 12; // q(1-q), Q12
        // (var<<12)/v is Q24 of q(1-q)/n, so isqrt lands in Q12
        uint16_t term = isqrt32(((uint32_t)var << 12) / v);
        int16_t lcb = (int16_t)q - (int16_t)(((uint32_t)term * LCB_Z) >> 8);
        if(lcb <= bestL) continue;
        if(m != MOVE_PASS && !game.isValidMove(m % BOARD_SIZE, m / BOARD_SIZE))
            continue;
        bestL = lcb;
        best = m;
    }
    if(best == MOVE_PASS) best = backup;
    if(best == MOVE_PASS) return 0;
    x = best % BOARD_SIZE;
    y = best / BOARD_SIZE;
    return 1;
}
