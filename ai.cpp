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
    resigned = 0;
    resignCount = 0;
    resignCount2 = 0;
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
// Extension nodes in ordinary statics. RAM here trades directly
// against stack headroom: at EXT 33 with UI strings in RAM the
// globals reached 2,383 bytes and think()'s call chain smashed the
// stack into them (the board filled with diagonal garbage). Moving
// the UI strings to PROGMEM bought the budget back — keep total free
// RAM >= ~250 for the stack. Total pool must stay < 255 (8-bit
// links, 0xFF = null).
#define NODE_POOL_EXT 69
#define NODE_POOL (NODE_POOL_SB + NODE_POOL_EXT) // must stay < 255 (8-bit links)
#ifndef MCTS_ITERATIONS
#define MCTS_ITERATIONS 400 // must stay under ~3400: 12-bit visit counters
#endif
#ifndef RAVE_K
#define RAVE_K 300          // Gelly-Silver beta schedule constant
#endif
// Q8 sigma for the root LCB pick. 1.6 sigma (410): measured on a
// 20-seed stability probe of a real misplay position, raising from
// 1.28 sigma removed exactly the one unlucky under-sampled pick (a
// pointless clamp) while leaving all 19 other picks unchanged —
// the "even out the luck" knob. Higher added nothing; raising the
// visit gate instead scattered picks.
#ifndef LCB_Z
#define LCB_Z 410
#endif
// Minimum real visits before a child may win the LCB race: below
// this, prior-seeded win rates are still mostly noise
#ifndef LCB_GATE
#define LCB_GATE 24
#endif
// Relative gate: an LCB candidate also needs visits >= maxVisits /
// LCB_REL_DIV. The absolute gate alone let a 28-visit child with a
// lucky-streak q beat an 84-visit leader and throw away a won game;
// siblings within 2x of the leader are sampled well enough to compare.
#ifndef LCB_REL_DIV
#define LCB_REL_DIV 2
#endif
// Exploration term scaled down by this shift: at 400 iterations over
// ~56 root moves, full UCB1-Tuned exploration (~0.3 at n=10) dwarfs
// the real q spread (~0.1) and the search polls uniformly instead of
// concentrating on the best line.
#ifndef UCB_EXPLORE_SHIFT
#define UCB_EXPLORE_SHIFT 1
#endif

// Virtual win/visit priors seeded at expansion; real playout results
// accumulate on top and wash these out. The base is deliberately heavy
// (michi's "even prior"): at weight 2 a single lucky playout swung a
// child's q from 0.50 to 0.67 and selection chased noise; at weight 8
// early q is stable and selection follows the bonus ordering until
// real evidence accumulates.
#ifndef PRIOR_BASE_V
#define PRIOR_BASE_V 8
#endif
#ifndef PRIOR_BASE_W
#define PRIOR_BASE_W 4
#endif
#ifndef PRIOR_CAPTURE
#define PRIOR_CAPTURE 6     // captures an enemy group (its last liberty)
#endif
#ifndef PRIOR_SAVE
#define PRIOR_SAVE 4        // extends an own group out of atari
#endif
#ifndef PRIOR_ATARI
#define PRIOR_ATARI 2       // puts an enemy group into atari
#endif
#ifndef PRIOR_CENTER_MAX
#define PRIOR_CENTER_MAX 2  // + min(distance from board edge, this)
#endif
#ifndef PRIOR_LOCAL
#define PRIOR_LOCAL 2       // adjacent or diagonal to the previous move
#endif
#ifndef PRIOR_PATTERN
#define PRIOR_PATTERN 3     // matches a local 3x3 shape pattern
#endif
#ifndef PRIOR_BIG
#define PRIOR_BIG 2         // big open point (far from every stone)
#endif
#ifndef PRIOR_OPEN_CORNER
#define PRIOR_OPEN_CORNER 4 // corner point of an untouched quadrant
#endif

// Low-line discipline, two-layered: during the opening (below
// EARLY_STONES stones) every non-tactical line-1/line-2 move is
// penalized unconditionally — a 2-2 slide near a contested corner is
// still an opening mistake even though an enemy is nearby. Past the
// opening the rule turns positional: line 1 stays penalized, line 2
// is exempt when an enemy stone is within distance 2, which is what
// real boundary and endgame plays look like.
#ifndef EARLY_STONES
#define EARLY_STONES 20
#endif
#ifndef PRIOR_EDGE_PENALTY
#define PRIOR_EDGE_PENALTY 5    // first line
#endif
#ifndef PRIOR_LINE2_PENALTY
#define PRIOR_LINE2_PENALTY 3   // second line
#endif

// A non-tactical move whose merged group lands at <=2 liberties is a
// thin stretch: easy to disconnect and chase. At exactly one liberty
// it is outright self-atari and eats a much larger penalty.
#ifndef PRIOR_THIN_PENALTY
#define PRIOR_THIN_PENALTY 4
#endif
#ifndef PRIOR_SELFATARI_PENALTY
#define PRIOR_SELFATARI_PENALTY 8
#endif

// Extending a group whose ladder is lost just feeds stones to the
// capture. Playouts cannot see this (they let doomed groups escape by
// randomness), so the prior must. Strength-tested hard (2026-07):
// an apparent 2x win-rate gain from removing it did NOT replicate on
// an independent seed set (10 vs 12 of 160) — it is strength-neutral
// and kept for the visible behavior it prevents. Note the reader's
// defender can only extend, never counter-capture, so "doomed" runs
// pessimistic in messy fights.
#ifndef PRIOR_FEED_PENALTY
#define PRIOR_FEED_PENALTY 6
#endif

// Capture race: filling a liberty of a low-liberty enemy chain that
// is racing one of our own low-liberty chains (see raceWin). Default
// ZERO: measured strength-neutral vs gnugo (36 vs 34 of 640), and in
// the OPENING every contact exchange sits at 2-3 liberties, so the
// bonus made early play contact-crazy — Jay reported the opening
// felt clearly worse with it on. Re-enable via -D for experiments.
#ifndef PRIOR_RACE
#define PRIOR_RACE 0
#endif

// Blocking a contact push (mid-game): the opponent's last stone
// touches our stones and the candidate completes the wall. Playout
// evaluation is blind to the 2-3 points each unanswered boundary
// push costs — territorial games bled out one quiet push at a time —
// so the tree gets steered to at least read the block.
#ifndef PRIOR_BLOCK
#define PRIOR_BLOCK 3
#endif

// Urgency: reinforcing an own 2-liberty group near the opponent's
// last move. Playouts resolve fights by coin flip, so the tree sees a
// fight move and a tenuki big-point as equal — and walks away from
// burning fights. This is what makes it stay.
#ifndef PRIOR_URGENT
#define PRIOR_URGENT 5
#endif

// Connection and cutting. The WEAK forms fire when the weakest chain
// involved has exactly 2 liberties — real danger of being split off
// (1-liberty chains are handled by the ladder-verified save). The
// plain PRIOR_CONNECT fires earlier: mid-game, when the candidate is
// the SOLE connector of two chains (see soleConnector) — defend the
// cutting point BEFORE the cut. The miai test is what keeps this
// from over-connecting (a generic two-chain bonus measurably
// tanked); a generic cut bonus pays for suicidal wedges, so cutting
// stays weak-only.
#ifndef PRIOR_CONNECT
#define PRIOR_CONNECT 4
#endif
// Knight-link defense: keima/ogeima/jump links have NO single point
// adjacent to both chains (the waists each touch one stone), so the
// sole-connector logic is structurally blind to them. This fires for
// a candidate in the gap — adjacent to one chain, a DIFFERENT chain
// within distance 2 — while an enemy stone touches the gap point:
// the push-through is happening now.
#ifndef PRIOR_LINK
#define PRIOR_LINK 3
#endif
#ifndef PRIOR_CONNECT_WEAK
#define PRIOR_CONNECT_WEAK 6
#endif
#ifndef PRIOR_CUT_WEAK
#define PRIOR_CUT_WEAK 5
#endif

// Naked attachment: contact with a HEALTHY enemy chain (3+ libs)
// with no orthogonal friendly support and no tactical purpose —
// "don't attach to strong stones". Mild: legitimate attachments
// carry support or a tactical tag. (Seen live: a pointless clamp
// against a supported chain, picked from a marginal LCB race.)
#ifndef PRIOR_ATTACH_PENALTY
#define PRIOR_ATTACH_PENALTY 2
#endif

// Empty triangle: the candidate completes three stones on a 2x2
// square whose fourth point is EMPTY — the classic inefficient
// shape ("the devil's own shape for wasting a move"). Exempt when
// tactical; a square whose fourth point holds an ENEMY stone is a
// fighting shape and never matches (the emptiness check is the
// classical exemption for free). Seen live: a blocking move played
// as the ugly-triangle variant when better-shaped blocks existed.
#ifndef PRIOR_EMPTY_TRI
#define PRIOR_EMPTY_TRI 2
#endif

// A candidate whose ONLY link to friendly stones is a knight's move,
// with enemy support around the waist points, is an extension that
// can be pushed through and cut immediately.
#ifndef PRIOR_KEIMA_PENALTY
#define PRIOR_KEIMA_PENALTY 4
#endif

// Moves inside settled territory (see regionVital): filling one's
// own loses a point; invading the opponent's gifts a prisoner. The
// region's VITAL point is the opposite: it decides simple life and
// death, and playouts cannot be trusted to find it on their own.
#ifndef PRIOR_OWNFILL_PENALTY
#define PRIOR_OWNFILL_PENALTY 6
#endif
#ifndef PRIOR_INVADE_PENALTY
#define PRIOR_INVADE_PENALTY 8
#endif
#ifndef PRIOR_VITAL
#define PRIOR_VITAL 10
#endif

// Only the first moves of a playout feed RAVE: a point that gets
// filled successfully in the endgame of most playouts says nothing
// about playing it NOW, and those stats were drowning the priors.
#ifndef RAVE_HORIZON
#define RAVE_HORIZON 24
#endif

// Uncertainty extension: if the visit leader has not reached this
// many visits when the budget runs out, the root is FLAT — too many
// live candidates sharing too few visits — and the LCB pick becomes
// a raffle among whichever few crossed the gate by luck. The blunder
// hunt's biggest drops (-14 to -28 pts) all came from flat roots,
// with the winning move sometimes nearly unvisited. One extra
// half-budget, granted once, self-targets exactly those positions.
#ifndef UNCERTAIN_MIN
#define UNCERTAIN_MIN 48
#endif

// Resignation: real-playout win rate under ~8% (1/12) for this many
// consecutive searches, past the opening. Light playouts keep a
// 5-10% swindle floor even in dead positions, so a reading this low
// means truly hopeless; the streak guards against one noisy search.
#ifndef RESIGN_DENOM
#define RESIGN_DENOM 12
#endif
// Second tier: a milder floor held for LONGER. Some dead positions
// keep a swindle-floor eval above the strict 1/12 forever and the
// AI flails on for 20+ moves — the blunder hunt's largest class by
// count was moves in such games. Five consecutive sub-1/6 reads is
// a corpse, not a fight.
#ifndef RESIGN2_DENOM
#define RESIGN2_DENOM 6
#endif
#ifndef RESIGN2_STREAK
#define RESIGN2_STREAK 5
#endif
#ifndef RESIGN_STREAK
#define RESIGN_STREAK 2
#endif
#ifndef RESIGN_MIN_STONES
#define RESIGN_MIN_STONES 25
#endif

// Progressive widening (non-root): children allowed = 1 + visits/RATE
#ifndef WIDEN_RATE
#define WIDEN_RATE 6
#endif
// Root progressive widening: start with the best ROOT_INIT prior
// candidates (plus pass) and widen by visits. Full root expansion
// diluted 400-600 iterations over 25-40 candidates (~15 visits each)
// — the blunder hunt showed flat roots picking raffle winners while
// the actual best move sat under 10 visits, twice with the winning
// move absent from the >=10-visit table entirely.
#ifndef ROOT_INIT
#define ROOT_INIT 8
#endif
#ifndef ROOT_WIDEN_RATE
#define ROOT_WIDEN_RATE 12
#endif
#ifndef WIDEN_CAP
#define WIDEN_CAP 16
#endif
// A leaf must collect this many visits (prior seed included) before it
// may grow a child: playouts from a cold leaf are as informative as a
// one-child subtree, and each expansion costs a full prior scan.
// Tactically hot leaves carry big seeds, so they still expand at once.
#ifndef EXPAND_VISITS
#define EXPAND_VISITS 8
#endif
// Playouts should run until the position resolves: truncating scores
// every not-yet-dead group as alive. ~90-120 moves settles an early
// position; 160 keeps the runaway guard from bending evaluations
// while staying affordable on the device.
#ifndef PLAYOUT_CAP
#define PLAYOUT_CAP 160
#endif
// Lone-invader answer probability mask (3 = fire 3/4 of the time,
// stacking with the local-answer step to ~7/8; 0 disables)
#ifndef LONE_ANSWER_MASK
#define LONE_ANSWER_MASK 3
#endif
// Extra search on the first out-of-book moves: below this many
// stones, iterations get a +50% boost. The board is at its most open
// exactly where playout evaluation is flattest, and the first search
// move after book exit was a measured, repeated 14-26 point blunder.
// 0 disables.
#ifndef OPENING_BOOST_STONES
#define OPENING_BOOST_STONES 14
#endif
#ifndef MERCY_MARGIN
#define MERCY_MARGIN 25     // capture lead that ends a playout early
#endif
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
    uintptr_t a = (uintptr_t)p;
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
static uint8_t freeHead;   // recycled nodes, threaded via nextSibling
static uint8_t path[32];   // current descent, root first (see reclaim)
static uint8_t pathDepth;
static uint8_t rootTurn;
static uint8_t rootKo;
static uint8_t rootLast; // opponent's actual last move (0xFF = none)
static uint8_t rootStones; // stones on the real board at think() time
static uint8_t simKomi;
// Real-playout tally for the whole search: the seed-free, average
// (not max-biased) evaluation of the root position. Read by the host
// test tools; costs two counters on-device.
static uint16_t thinkSims, thinkSimWins;
// Vital points of small eyespaces on the ROOT board, found once per
// search. Playouts probe these no matter where the last move was —
// otherwise a tenuki from a life-and-death spot is never punished in
// rollouts and scores as well as resolving it.
static uint8_t rootVitals[3];
static uint8_t nRootVitals;
// Liberty carryover: a gated simPlay floods the placed group anyway;
// the next playout move classifies that same group on an unchanged
// board, so the result is cached instead of re-flooded.
static uint8_t cacheLibsPos; // group's stone position, 0xFF = invalid
static uint8_t cacheLibs, cacheL1, cacheL2;
static uint8_t simCaptured;  // stones captured by the last simPlay
static uint8_t simBoard[BOARD_CELLS];
static uint8_t simMark[BOARD_CELLS];   // epoch marks for flood fill
// Chain map, computed once per EXPANSION while the board is frozen.
// One byte per cell: (libs << 6) | id — the capped 1/2/3+ liberty
// class lives in the top 2 bits, the chain id in the low 6 (0 =
// empty cell). Legal positions max out near ~40 chains (the safe
// theoretical bound is ~64); ids saturate at 63, which degrades
// identity precision in impossible positions instead of anything
// worse. Replaces per-candidate chain floods (once ~38% of think
// time). Valid only inside one expandNode/widenNode call.
static uint8_t chainId[BOARD_CELLS];
#define CHAIN_OF(b) ((b) & 0x3F)
#define LIBS_OF(b) ((b) >> 6)
static uint8_t markEpoch;
static uint16_t rngState;
// Host-harness tuning knobs. On the device they compile to constants
// and cost neither RAM nor cycles.
#ifdef ARDUINO
#define mctsIterations MCTS_ITERATIONS
#define reclaimEnabled 1
#define resignStreak RESIGN_STREAK
#else
uint16_t mctsIterations = MCTS_ITERATIONS;
uint8_t reclaimEnabled = 1;
uint8_t resignStreak = RESIGN_STREAK;
#endif
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

static uint8_t groupLibsCore(uint8_t start, uint8_t *l1, uint8_t *l2,
                             uint8_t markAll, uint8_t cap);

// Does the group at start have any liberty? Thin wrapper — the
// shared flood's seed fast path handles the common case identically;
// only big-group floods do modestly more work, and the ~180 bytes of
// flash matter more than those cycles.
static uint8_t hasLiberty(uint8_t start) {
    uint8_t a, b;
    return groupLibsCore(start, &a, &b, 0, 1) != 0;
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

static uint8_t groupLibsFind(uint8_t start, uint8_t *l1, uint8_t *l2);

// If the group at start has exactly one liberty, return it; else 0xFF.
// (Thin wrapper: groupLibsFind's early-exit-at-3 does the same flood
// with marginally more work than a dedicated exit-at-2 — the ~150
// bytes of flash matter more than those cycles.)
static uint8_t soleLiberty(uint8_t start) {
    uint8_t a, b;
    return groupLibsCore(start, &a, &b, 0, 2) == 1 ? a : 0xFF;
}

// Shared flood core for ALL the liberty finders: counts distinct
// liberties, early-exiting once `cap` are found (1 = hasLiberty,
// 2 = soleLiberty, 3 = full find — profiling showed hasLiberty
// routed through a fixed cap-3 version was HALF of all search time).
// With markAll it floods the WHOLE group into the CURRENT mark epoch
// (membership tests need complete marking; cap is ignored).
static uint8_t groupLibsCore(uint8_t start, uint8_t *l1, uint8_t *l2,
                             uint8_t markAll, uint8_t cap) {
    uint8_t color = simBoard[start];
    uint8_t nb[4];
    uint8_t lib1 = 0xFF, lib2 = 0xFF;
    uint8_t count = 0;

    if(!markAll) {
        // Seed fast path: enough empty neighbors of the seed settles
        // it before paying for the flood setup — the common case.
        uint8_t n0 = neighbors(start, nb);
        for(uint8_t i = 0; i < n0; i++) {
            if(simBoard[nb[i]] != EMPTY) continue;
            if(lib1 == 0xFF) lib1 = nb[i];
            else if(lib2 == 0xFF) lib2 = nb[i];
            count++;
            if(count >= cap) {
                *l1 = lib1;
                *l2 = lib2;
                return count;
            }
        }
        newMark();
    }

    uint8_t sp = 0;
    floodSlot(sp++) = start;
    simMark[start] = markEpoch;
    while(sp && (markAll || count < cap)) {
        uint8_t p = floodSlot(--sp);
        uint8_t n = neighbors(p, nb);
        for(uint8_t i = 0; i < n; i++) {
            uint8_t q = nb[i];
            if(simBoard[q] == EMPTY) {
                if(count < 3 && q != lib1 && q != lib2) {
                    if(lib1 == 0xFF) lib1 = q;
                    else if(lib2 == 0xFF) lib2 = q;
                    count++;
                    if(!markAll && count >= cap) break;
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

// Find a group's distinct liberties, early-exiting at 3. Fills l1/l2
// with the first two (0xFF if fewer). Returns the count, 0-3.
static uint8_t groupLibsFind(uint8_t start, uint8_t *l1, uint8_t *l2) {
    return groupLibsCore(start, l1, l2, 0, 3);
}

static uint8_t groupLibsMax3(uint8_t start) {
    uint8_t a, b;
    uint8_t n = groupLibsFind(start, &a, &b);
    return n ? n : 1; // preserve old behavior: 0 liberties reads as 1
}

// Full-flood variant of groupLibsFind (see groupLibsCore's markAll)
static uint8_t groupLibsMark(uint8_t start) {
    uint8_t a, b;
    return groupLibsCore(start, &a, &b, 1, 3);
}

// Build the chain map: flood each chain to assign ids and count
// liberties, then a second cheap walk stamps the libs bits (a live
// chain always has libs >= 1, so zero top bits doubles as the
// "not yet stamped" marker).
static void buildChainMap() {
    memset(chainId, 0, sizeof(chainId));
    uint8_t nextId = 0;
    uint8_t nb[4];
    for(uint8_t s = 0; s < BOARD_CELLS; s++) {
        if(simBoard[s] == EMPTY || chainId[s]) continue;
        uint8_t color = simBoard[s];
        uint8_t id = nextId < 63 ? ++nextId : 63;
        uint8_t lib1 = 0xFF, lib2 = 0xFF, count = 0;
        uint8_t sp = 0;
        floodSlot(sp++) = s;
        chainId[s] = id;
        while(sp) {
            uint8_t p = floodSlot(--sp);
            uint8_t n = neighbors(p, nb);
            for(uint8_t i = 0; i < n; i++) {
                uint8_t q = nb[i];
                if(simBoard[q] == EMPTY) {
                    if(count < 3 && q != lib1 && q != lib2) {
                        if(lib1 == 0xFF) lib1 = q;
                        else if(lib2 == 0xFF) lib2 = q;
                        count++;
                    }
                } else if(simBoard[q] == color && !chainId[q]) {
                    chainId[q] = id;
                    floodSlot(sp++) = q;
                }
            }
        }
        // stamp libs into the top bits of every member
        uint8_t bits = count << 6;
        sp = 0;
        floodSlot(sp++) = s;
        chainId[s] |= bits;
        while(sp) {
            uint8_t p = floodSlot(--sp);
            uint8_t n = neighbors(p, nb);
            for(uint8_t i = 0; i < n; i++) {
                uint8_t q = nb[i];
                if(chainId[q] == id) { // id match, libs not yet stamped
                    chainId[q] |= bits;
                    floodSlot(sp++) = q;
                }
            }
        }
    }
}

// pos touches two DISTINCT friendly chains (seeds fa, fb). Is it
// their ONLY connecting point? Re-flood each under its own mark
// epoch, then count empty points adjacent to both chains: exactly
// one (pos itself) means the opponent playing here splits us for
// real. Two or more means miai — no urgency, and bonusing those was
// how an earlier connect prior over-concentrated and measurably
// tanked. Distinct same-color chains are never adjacent, so the
// second flood cannot leak into the first.
static uint8_t soleConnector(uint8_t idA, uint8_t idB) {
    uint8_t nb[4];
    uint8_t connectors = 0;
    for(uint8_t p = 0; p < BOARD_CELLS; p++) {
        if(simBoard[p] != EMPTY) continue;
        uint8_t nearA = 0, nearB = 0;
        uint8_t n = neighbors(p, nb);
        for(uint8_t i = 0; i < n; i++) {
            uint8_t id = CHAIN_OF(chainId[nb[i]]);
            if(id == idA) nearA = 1;
            else if(id == idB) nearB = 1;
        }
        if(nearA && nearB && ++connectors > 1) return 0;
    }
    return connectors == 1;
}

// Flood the small empty region containing `seed` (a big region is
// neither settled territory nor an eyespace). Returns the color of
// the stones bordering it — 0 if it touches both colors or is too
// open — and sets *vital to the region's vital point, 0xFF if none.
//
// The vital point is the unique cell with the most neighbors inside
// the region (degree >= 2): center of a straight/bent three, center
// of a T/pyramid four, center of a bulky/cross five. Uniqueness
// makes the classics come out right by itself — square four and
// line four have no single deciding point and correctly yield none.
// Whoever plays the vital point decides the region's life: the owner
// splits it into two eyes, the opponent reduces it to one.
//
// Non-vital moves inside a settled region are pointless-to-harmful
// under the Japanese rules the game scores by (own fill: -1 point;
// hopeless invasion: gifts a prisoner), but the area-scoring
// playouts think they are free.
#define SETTLED_REGION_MAX 8
static uint8_t regionVital(uint8_t seed, uint8_t *vital) {
    uint8_t region[SETTLED_REGION_MAX];
    uint8_t cnt = 0, head = 0;
    uint8_t owner = 0;
    uint8_t nb[4];
    *vital = 0xFF;
    newMark();
    region[cnt++] = seed;
    simMark[seed] = markEpoch;
    while(head < cnt) {
        uint8_t n = neighbors(region[head++], nb);
        for(uint8_t i = 0; i < n; i++) {
            uint8_t q = nb[i];
            uint8_t s = simBoard[q];
            if(s != EMPTY) {
                if(!owner) owner = s;
                else if(owner != s) return 0;        // touches both colors
                continue;
            }
            if(simMark[q] == markEpoch) continue;
            if(cnt >= SETTLED_REGION_MAX) return 0;  // too open to be settled
            simMark[q] = markEpoch;
            region[cnt++] = q;
        }
    }
    if(owner && cnt >= 3 && cnt <= 6) {
        uint8_t bestDeg = 1, bestCell = 0xFF, ties = 0;
        for(uint8_t j = 0; j < cnt; j++) {
            uint8_t deg = 0;
            uint8_t n = neighbors(region[j], nb);
            for(uint8_t i = 0; i < n; i++)
                if(simBoard[nb[i]] == EMPTY && simMark[nb[i]] == markEpoch)
                    deg++;
            if(deg > bestDeg) {
                bestDeg = deg;
                bestCell = region[j];
                ties = 1;
            } else if(deg == bestDeg) {
                ties++;
            }
        }
        if(bestCell != 0xFF && ties == 1) *vital = bestCell;
    }
    return owner;
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
            // One flood covers both suicide (0 libs) and self-atari (1).
            // Cache the result: the next playout move classifies this
            // same group on an unchanged board.
            uint8_t a, b;
            uint8_t libs = groupLibsFind(pos, &a, &b);
            if(libs < 2) {
                simBoard[pos] = EMPTY;
                return ILLEGAL;
            }
            cacheLibsPos = pos;
            cacheLibs = libs;
            cacheL1 = a;
            cacheL2 = b;
        } else {
            if(!hasLiberty(pos)) { // suicide
                simBoard[pos] = EMPTY;
                return ILLEGAL;
            }
            // Ungated success: no flood ran, so no cache. Invalidate —
            // a capture may even have re-emptied the cached position.
            cacheLibsPos = 0xFF;
        }
    } else {
        cacheLibsPos = 0xFF;
    }
    simCaptured = captured;

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
static uint8_t ladderEscapes(uint8_t defStart, uint8_t esc,
                             uint8_t maxSteps = 20) {
    memcpy(ladderBoard, simBoard, BOARD_CELLS);
    uint8_t defColor = simBoard[defStart];
    uint8_t atkColor = 3 - defColor;
    uint8_t escaped = 1;

    for(uint8_t step = 0; step < maxSteps; step++) {
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

// Playout capture tallies (statics so the shared move helper can
// update them; reset at each playout start)
static uint8_t capB, capW;

// Attempt one playout move at pos (0xFF = none): the legality gate,
// gated play, and bookkeeping shared by every playout heuristic.
// Returns 1 when the move was played; caller flips toMove.
__attribute__((noinline))
static uint8_t playoutTry(uint8_t pos, uint8_t toMove, uint8_t *ko,
                          uint8_t *last, uint8_t m) {
    if(pos >= BOARD_CELLS || pos == *ko || simBoard[pos] != EMPTY ||
       isOwnEye(pos, toMove)) return 0;
    uint8_t nk = simPlay(pos, toMove, *ko, 1);
    if(nk == ILLEGAL) return 0;
    if(toMove == rootTurn && m < RAVE_HORIZON) raveMark(pos);
    if(simCaptured) {
        if(toMove == BLACK) capB += simCaptured;
        else capW += simCaptured;
    }
    *ko = nk;
    *last = pos;
    return 1;
}

// Random playout from current simBoard; returns winning color.
// `last` = the previous move (0xFF/pass = none).
static uint8_t playout(uint8_t toMove, uint8_t ko, uint8_t last) {
    uint8_t passes = 0;
    capB = capW = 0;
    for(uint8_t m = 0; m < PLAYOUT_CAP && passes < 2; m++) {
        // Mercy rule: a lopsided capture balance has decided the game;
        // the area score already reflects it, skip the remaining fill
        if(capB > capW + MERCY_MARGIN || capW > capB + MERCY_MARGIN)
            break;

        // Tactical: if the opponent's just-moved group is in atari,
        // capture it. Uniform-random playouts ignore atari, which
        // wrecks every life-and-death estimate.
        // Heuristics fire probabilistically (michi-style): applied
        // every time, all playouts make the SAME systematic errors and
        // those don't average out. Tactical ~7/8, patterns ~15/16.
        if(last < BOARD_CELLS && simBoard[last] != EMPTY && (rnd16() & 7)) {
            uint8_t tac = 0xFF, isSave = 0;

            // Classify the opponent's just-moved group — free when
            // their gated simPlay already flooded it (board unchanged)
            uint8_t l1, l2, libs;
            if(cacheLibsPos == last) {
                libs = cacheLibs;
                l1 = cacheL1;
                l2 = cacheL2;
            } else {
                libs = groupLibsFind(last, &l1, &l2);
            }
            if(libs == 1 && l1 != ko) {
                // Capture the atari'd group
                tac = l1;
            } else if(libs == 2 && (rnd16() & 1)) {
                // Squeeze a 2-liberty group: fill one of its
                // liberties. (A 3/4 rate was tried with the race
                // reader and made opening rollouts contact-crazy.)
                // This is what actually kills disconnected stones in
                // playouts — without it, cut-off groups survive by
                // randomness and thin extensions look safe.
                uint8_t cand = (rnd16() & 1) ? l1 : l2;
                if(cand == ko) cand = (cand == l1) ? l2 : l1;
                tac = cand;
                isSave = 1; // route through the self-atari gate
            } else {
                // Else escape: their move may have put an own group
                // next to it into atari — extend at its last liberty
                uint8_t nb[4];
                uint8_t n = neighbors(last, nb);
                for(uint8_t i = 0; i < n; i++) {
                    if(simBoard[nb[i]] != toMove) continue;
                    uint8_t sl = soleLiberty(nb[i]);
                    // Short read: playouts rarely need long ladders,
                    // and a truncated read defaults to "escape"
                    if(sl != 0xFF && sl != ko &&
                       ladderEscapes(nb[i], sl, 8)) {
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
                    if(simCaptured) {
                        if(toMove == BLACK) capB += simCaptured;
                        else capW += simCaptured;
                    }
                    ko = nk;
                    last = tac;
                    passes = 0;
                    toMove = 3 - toMove;
                    continue;
                }
            }
        }

        // Simple life & death: if the last move borders a small
        // one-color eyespace, its vital point decides life — take it
        // (deny the second eye, or split own space into two). This
        // is what makes dead shapes actually die in playouts instead
        // of surviving on randomness.
        if(last < BOARD_CELLS && (rnd16() & 3)) {
            uint8_t vcand = 0xFF;
            uint8_t nbv[4];
            uint8_t nv = neighbors(last, nbv);
            for(uint8_t i = 0; i < nv; i++) {
                if(simBoard[nbv[i]] != EMPTY) continue;
                uint8_t vital;
                if(regionVital(nbv[i], &vital)) vcand = vital;
                break; // only the first adjacent region
            }
            if(playoutTry(vcand, toMove, &ko, &last, m)) {
                passes = 0;
                toMove = 3 - toMove;
                continue;
            }
        }

        // Root-board vital points: grab one if still open (1/8 per
        // move — hotter distorts ordinary endgame playouts). The
        // local hook above only reacts when the last move touches
        // the eyespace — this is what makes tenuki from a
        // life-and-death spot actually lose rollouts.
        if(nRootVitals && !(rnd16() & 7)) {
            uint8_t vp = 0xFF;
            for(uint8_t i = 0; i < nRootVitals; i++)
                if(simBoard[rootVitals[i]] == EMPTY) {
                    vp = rootVitals[i];
                    break;
                }
            if(playoutTry(vp, toMove, &ko, &last, m)) {
                passes = 0;
                toMove = 3 - toMove;
                continue;
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
            if(nMatches &&
               playoutTry(matches[rnd(nMatches)], toMove, &ko, &last, m)) {
                passes = 0;
                toMove = 3 - toMove;
                continue;
            }
        }

        // Local answer: half the time, try one random point around
        // the last move before the global probe — plain contact
        // resistance the patterns can't express. A LONE last stone
        // (no support within distance 2 — an invasion or deep
        // reduction) additionally gets a contact reply on 3/4 of the
        // remaining coin, ~7/8 total: without that, random invasions
        // of settled territory live far too often, the single
        // biggest playout evaluation bias.
        if(last < BOARD_CELLS && simBoard[last] != EMPTY) {
            uint16_t p = rnd16();
            uint8_t pos = 0xFF;
            int8_t lx = last % BOARD_SIZE, ly = last / BOARD_SIZE;
            if(p & 1) {
                int8_t cx = lx + (int8_t)rnd(3) - 1;
                int8_t cy = ly + (int8_t)rnd(3) - 1;
                if(cx >= 0 && cx < BOARD_SIZE && cy >= 0 && cy < BOARD_SIZE)
                    pos = cy * BOARD_SIZE + cx;
            } else if(rootStones + m >= EARLY_STONES &&
                      (p & LONE_ANSWER_MASK << 1) != 0) {
                // (gated: in the opening EVERY stone is "lone" and
                // every attachment is normal — these answer rules
                // only mean something once territory has shape)
                // Answer a stone that CONTACTS us (a boundary push)
                // or one with no support within 2 (an invasion);
                // either way the reply is a contact move.
                uint8_t lastColor = simBoard[last];
                uint8_t nbl[4];
                uint8_t nl = neighbors(last, nbl);
                uint8_t answer = 0;
                for(uint8_t j = 0; j < nl; j++)
                    if(simBoard[nbl[j]] == toMove) {
                        answer = 1; // contact push
                        break;
                    }
                if(!answer) {
                    answer = 1; // lone unless support found
                    for(int8_t dy = -2; dy <= 2 && answer; dy++)
                        for(int8_t dx = -2; dx <= 2; dx++) {
                            if(!dx && !dy) continue;
                            int8_t nx = lx + dx, ny = ly + dy;
                            if(nx < 0 || nx >= BOARD_SIZE ||
                               ny < 0 || ny >= BOARD_SIZE) continue;
                            if(simBoard[ny * BOARD_SIZE + nx] == lastColor) {
                                answer = 0;
                                break;
                            }
                        }
                }
                if(answer) pos = nbl[rnd(nl)];
            }
            if(pos != 0xFF && playoutTry(pos, toMove, &ko, &last, m)) {
                passes = 0;
                toMove = 3 - toMove;
                continue;
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

            // Territory bias: filling deep empty space at random
            // splits open areas 50/50 no matter who holds the
            // boundary, hiding the value of quiet boundary moves
            // from the eval. Past the opening, 3/4 of the time defer
            // isolated points (no stone in the 8-neighborhood) and
            // grow from existing structure instead.
#ifndef PLAYOUT_GROW_MASK
#define PLAYOUT_GROW_MASK 3
#endif
            if(nn == 4 && rootStones + m >= EARLY_STONES &&
               (rnd16() & PLAYOUT_GROW_MASK)) {
                uint8_t bx = pos % BOARD_SIZE, by = pos / BOARD_SIZE;
                uint8_t touch = 0;
                for(int8_t tdy = -1; tdy <= 1 && !touch; tdy++)
                    for(int8_t tdx = -1; tdx <= 1; tdx++) {
                        if(!tdx && !tdy) continue;
                        int8_t tx = bx + tdx, ty = by + tdy;
                        if(tx < 0 || tx >= BOARD_SIZE ||
                           ty < 0 || ty >= BOARD_SIZE) continue;
                        if(simBoard[ty * BOARD_SIZE + tx] != EMPTY) {
                            touch = 1;
                            break;
                        }
                    }
                if(!touch) continue;
            }

            if(!playoutTry(pos, toMove, &ko, &last, m)) continue;
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
    uint8_t i;
    if(freeHead != 0xFF) {
        i = freeHead;
        freeHead = node(i).nextSibling;
    } else if(poolUsed < NODE_POOL) {
        i = poolUsed++;
    } else {
        return 0xFF;
    }
    Node &n = node(i);
    n.move = move;
    n.firstChild = 0xFF;
    n.nextSibling = 0xFF;
    n.s[0] = n.s[1] = n.s[2] = 0;
    return i;
}

// Return every descendant of v (not v itself) to the free list. The
// sibling links double as the traversal worklist, so no stack is
// needed: a node's children are spliced into the list ahead of its
// remaining siblings before it is freed.
static void freeSubtree(uint8_t v) {
    uint8_t work = node(v).firstChild;
    node(v).firstChild = 0xFF;
    while(work != 0xFF) {
        uint8_t nxt = node(work).nextSibling;
        uint8_t fc = node(work).firstChild;
        if(fc != 0xFF) {
            uint8_t t = fc;
            while(node(t).nextSibling != 0xFF) t = node(t).nextSibling;
            node(t).nextSibling = nxt;
            nxt = fc;
        }
        node(work).nextSibling = freeHead;
        freeHead = work;
        work = nxt;
    }
}

// Pool dry: recycle the subtree under the least-visited off-path
// sibling of the descent path. The victim keeps its own stats — root
// moves stay choosable by bestMove, and a revisit re-widens it from
// scratch — only its descendants go back to the free list. Skipping
// path[] members keeps the active descent's indices valid. Returns 1
// if anything was freed.
static uint8_t reclaim() {
    uint8_t victim = 0xFF;
    uint16_t worst = 0xFFFF;
    for(uint8_t d = 0; d < pathDepth; d++) {
        uint8_t next = (d + 1 < pathDepth) ? path[d + 1] : 0xFF;
        for(uint8_t c = node(path[d]).firstChild; c != 0xFF;
            c = node(c).nextSibling) {
            if(c == next || node(c).firstChild == 0xFF) continue;
            uint16_t v = nVisits(c);
            if(v < worst) {
                worst = v;
                victim = c;
            }
        }
    }
    if(victim == 0xFF) return 0;
    freeSubtree(victim);
    return 1;
}

// A node can be allocated now, or after recycling a dead subtree.
// Checked BEFORE the prior scans in expand/widen, so a hopeless scan
// is never paid for.
static uint8_t allocReady() {
    if(freeHead != 0xFF || poolUsed < NODE_POOL) return 1;
    return reclaimEnabled && reclaim();
}

// Corner quadrants (4x4 regions) with no stones, as bits TL,TR,BL,BR.
// Filled by buildNearMask, consumed by candidatePrior.
static uint8_t emptyCorners;

// 81-bit bitmap of points within distance 2 of any stone.
// Returns 0 if the board has no stones (then allow everything).
static uint8_t buildNearMask(uint8_t *near) {
    memset(near, 0, 11);
    uint8_t anyStone = 0;
    emptyCorners = 0x0F;
    for(uint8_t p = 0; p < BOARD_CELLS; p++) {
        if(simBoard[p] == EMPTY) continue;
        anyStone = 1;
        uint8_t sx = p % BOARD_SIZE, sy = p / BOARD_SIZE;

        // Corner occupancy (stones on the center lines claim none)
        if(sx != 4 && sy != 4) {
            if(sx <= 3 && sy <= 3)      emptyCorners &= ~1;
            else if(sx >= 5 && sy <= 3) emptyCorners &= ~2;
            else if(sx <= 3 && sy >= 5) emptyCorners &= ~4;
            else if(sx >= 5 && sy >= 5) emptyCorners &= ~8;
        }

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

// Light capture-race reader. The enemy chain at eStart has few
// liberties; a race is on if it also touches a low-liberty friendly
// chain. Flood the enemy chain (fresh epoch), then scan its stones'
// friendly neighbors for the weakest adjacent friendly chain — all
// marks share the epoch, so nothing is counted twice. The mover wins
// equal races (fill their outside liberties, they fill ours, we land
// first), so filling is winning iff enemyLibs <= friendMinLibs.
// Shared liberties count for both sides and roughly cancel — this is
// a tempo counter, not a seki solver.
#if PRIOR_RACE
static uint8_t raceWin(uint8_t eStart) {
    uint8_t eColor = simBoard[eStart];
    uint8_t nb[4];
    newMark();
    uint8_t eLibs = groupLibsMark(eStart);
    if(eLibs > 3) return 0;
    uint8_t fMin = 0xFF;
    for(uint8_t p = 0; p < BOARD_CELLS; p++) {
        if(simBoard[p] != eColor || simMark[p] != markEpoch) continue;
        uint8_t n = neighbors(p, nb);
        for(uint8_t i = 0; i < n; i++) {
            uint8_t q = nb[i];
            if(simBoard[q] == 3 - eColor && simMark[q] != markEpoch) {
                uint8_t fl = groupLibsMark(q); // joins the same epoch
                if(fl < fMin) fMin = fl;
            }
        }
    }
    return fMin <= 3 && eLibs <= fMin;
}
#endif

// Any enemy stone in the 3x3 around (cx,cy)?
static uint8_t oppNear(int8_t cx, int8_t cy, uint8_t opp) {
    for(int8_t dy = -1; dy <= 1; dy++)
        for(int8_t dx = -1; dx <= 1; dx++) {
            int8_t nx = cx + dx, ny = cy + dy;
            if(nx < 0 || nx >= BOARD_SIZE || ny < 0 || ny >= BOARD_SIZE)
                continue;
            if(simBoard[ny * BOARD_SIZE + nx] == opp) return 1;
        }
    return 0;
}

// Prior for one candidate: tactics + center + locality + shape, minus
// early-game low-line penalties. Negative = virtual losses. isFar
// marks a big open point, which can never earn tactical or local
// credit and gets its own bonus instead.
static int8_t candidatePrior(uint8_t pos, uint8_t toMove, uint8_t last,
                             uint8_t isFar) {
    uint8_t opp = 3 - toMove;
    uint8_t sawCapture = 0, sawSave = 0, sawAtari = 0, sawDoomed = 0;
    uint8_t sawWeakFriend = 0;
    uint8_t hasOrthFriend = 0;
    uint8_t nb[4];
    uint8_t n = neighbors(pos, nb);

    // Group-aware neighbor scan over the precomputed chain map
    // (buildChainMap runs once per expansion): distinct chains per
    // color and their weakest liberty classes — pure array reads,
    // no floods (the per-candidate chain floods here were the
    // biggest single cost in the whole search).
    uint8_t fGroups = 0, eGroups = 0;
    uint8_t fMinLibs = 0xFF, eMinLibs = 0xFF;
    uint8_t doomCand[4];
    uint8_t nDoom = 0;
    uint8_t fIds[4];  // ids of the distinct friendly chains seen
    uint8_t seen[4];
    uint8_t nSeen = 0;
#if PRIOR_RACE
    uint8_t raceCand = 0xFF; // enemy chain at 2-3 libs, race check later
#endif
    for(uint8_t j = 0; j < n; j++) {
        uint8_t q = nb[j];
        uint8_t id = CHAIN_OF(chainId[q]);
        if(!id) continue;
        uint8_t dup = 0;
        for(uint8_t k = 0; k < nSeen; k++)
            if(seen[k] == id) { dup = 1; break; }
        if(dup) continue;
        seen[nSeen++] = id;
        uint8_t l = LIBS_OF(chainId[q]);
        if(simBoard[q] == opp) {
            eGroups++;
            if(l < eMinLibs) eMinLibs = l;
            if(l == 1) sawCapture = 1;      // pos is its last liberty
            else if(l == 2) sawAtari = 1;
#if PRIOR_RACE
            if(l >= 2 && l <= 3 && raceCand == 0xFF) raceCand = q;
#endif
        } else {
            hasOrthFriend = 1;
            if(fGroups < 4) fIds[fGroups] = id;
            fGroups++;
            if(l < fMinLibs) fMinLibs = l;
            if(l == 1) doomCand[nDoom++] = q;
            else if(l == 2) sawWeakFriend = 1;
        }
    }
    // Knight-link defense (see PRIOR_LINK). Must run while the scan's
    // marks are fresh: the adjacent chains are whole-chain marked, so
    // an UNMARKED friendly stone within distance 2 is a genuinely
    // different chain — a keima/ogeima/jump partner across the gap.
    uint8_t sawLink = 0;
    if(fGroups >= 1 && last < BOARD_CELLS) {
        uint8_t px = pos % BOARD_SIZE, py = pos / BOARD_SIZE;
        // The pressure must be the CURRENT push, and the candidate
        // must TOUCH the pushing stone orthogonally: the junction
        // point does, the adjacent near-misses only diagonal it —
        // in a real cut-through game both scored alike until this.
        // (No stone-count gate: contact + existing structure is
        // self-gating; the opening-brawl bug was playout-side.)
        int8_t pdx = px - last % BOARD_SIZE; if(pdx < 0) pdx = -pdx;
        int8_t pdy = py - last / BOARD_SIZE; if(pdy < 0) pdy = -pdy;
        if(pdx + pdy == 1) {
            for(int8_t ldy = -2; ldy <= 2 && !sawLink; ldy++)
                for(int8_t ldx = -2; ldx <= 2; ldx++) {
                    int8_t nx = px + ldx, ny = py + ldy;
                    if(nx < 0 || nx >= BOARD_SIZE ||
                       ny < 0 || ny >= BOARD_SIZE) continue;
                    uint8_t q = ny * BOARD_SIZE + nx;
                    if(simBoard[q] == toMove) {
                        uint8_t id = CHAIN_OF(chainId[q]), other = 1;
                        for(uint8_t k = 0; k < fGroups && k < 4; k++)
                            if(fIds[k] == id) { other = 0; break; }
                        if(other) { sawLink = 1; break; }
                    }
                }
        }
    }

    // Only credit a save if the ladder actually works — extending a
    // ladder-dead group just feeds stones
    for(uint8_t j = 0; j < nDoom; j++) {
        if(ladderEscapes(doomCand[j], pos)) sawSave = 1;
        else sawDoomed = 1;
    }

    // Capture race (see raceWin): fill their liberties while ahead
    // on tempo. Skipped when the capture is already immediate.
    // PRIOR_RACE is a compile-time constant; at 0 this folds away.
    uint8_t sawRace = 0;
#if PRIOR_RACE
    if(raceCand != 0xFF && !sawCapture && raceWin(raceCand))
        sawRace = 1;
#endif

    int8_t bonus = sawCapture ? PRIOR_CAPTURE :
                   sawSave    ? PRIOR_SAVE :
                   sawRace    ? PRIOR_RACE :
                   sawAtari   ? PRIOR_ATARI : 0;

    // Feeding a lost ladder: the single most expensive habit the
    // playouts approve of (they let doomed groups live by randomness,
    // so the search happily throws stone after stone into a dead
    // group). Countering captures are exempt.
    if(sawDoomed && !sawCapture && !sawSave)
        bonus -= PRIOR_FEED_PENALTY;

    // Connection: this point joins a 2-liberty chain to another
    // friendly chain — the rescue move for a group about to be split
    // off and killed. Cutting is the mirror image. A hopeless
    // connection into self-atari still gets sunk by the thin-stretch
    // penalty below.
    // connHere also exempts the point from the settled-territory
    // penalty below: a sole connector often sits inside what reads
    // as own territory, and -6 was cancelling the bonus.
    uint8_t connHere = 0;
    if(fGroups >= 2) {
        if(fMinLibs == 2) {
            bonus += PRIOR_CONNECT_WEAK;
            connHere = 1;
        } else if(rootStones >= EARLY_STONES &&
                  soleConnector(fIds[0], fIds[1])) {
            bonus += PRIOR_CONNECT;
            connHere = 1;
        }
    }
    if(eGroups >= 2 && eMinLibs == 2) bonus += PRIOR_CUT_WEAK;
    if(sawLink && !connHere) bonus += PRIOR_LINK;

    // Eyespaces and settled territory. The vital point of a small
    // one-color region is simple life and death — make the second
    // eye or deny it — and gets a strong bonus for either side. Any
    // OTHER move inside a settled region is an own fill or a
    // hopeless invasion: invisible costs to the area-scoring
    // playouts.
    // vitalHere also exempts the move from the thin-stretch and
    // low-line penalties below: vital points are inherently thin,
    // low-line moves inside eyespaces, and the generic penalties
    // were burying the bonus.
    uint8_t vitalHere = 0;
    {
        uint8_t vital;
        uint8_t so = regionVital(pos, &vital);
        if(so && vital == pos) {
            bonus += PRIOR_VITAL;
            vitalHere = 1;
        } else if(so && !sawCapture && !sawSave && !sawAtari &&
                  !connHere) {
            bonus -= (so == toMove) ? PRIOR_OWNFILL_PENALTY
                                    : PRIOR_INVADE_PENALTY;
        }
    }

    // Empty triangle in either orientation (see PRIOR_EMPTY_TRI):
    // corner form = diagonal friend + one corner friend + other
    // corner empty; elbow form = both corners friendly + diagonal
    // point empty.
    uint8_t triHere = 0;
    if(!sawCapture && !sawSave && !sawAtari && !sawRace) {
        uint8_t tri = 0;
        uint8_t tx = pos % BOARD_SIZE, ty = pos / BOARD_SIZE;
        for(int8_t tdy = -1; tdy <= 1 && !tri; tdy += 2)
            for(int8_t tdx = -1; tdx <= 1; tdx += 2) {
                int8_t fx = tx + tdx, fy = ty + tdy;
                if(fx < 0 || fx >= BOARD_SIZE ||
                   fy < 0 || fy >= BOARD_SIZE) continue;
                uint8_t sd = simBoard[fy * BOARD_SIZE + fx];
                uint8_t s1 = simBoard[ty * BOARD_SIZE + fx];
                uint8_t s2 = simBoard[fy * BOARD_SIZE + tx];
                if(sd == toMove &&
                   ((s1 == toMove && s2 == EMPTY) ||
                    (s2 == toMove && s1 == EMPTY))) tri = 1;
                else if(sd == EMPTY && s1 == toMove && s2 == toMove)
                    tri = 1;
                if(tri) break;
            }
        if(tri) {
            bonus -= PRIOR_EMPTY_TRI;
            triHere = 1;
        }
    }

    // Naked attachment to a healthy chain (see PRIOR_ATTACH_PENALTY)
    if(!sawCapture && !sawSave && !sawAtari && !sawRace &&
       !hasOrthFriend && eGroups && eMinLibs >= 3)
        bonus -= PRIOR_ATTACH_PENALTY;

    // Urgent defense: reinforcing an own 2-liberty group in the zone
    // of the opponent's last move — don't tenuki from a live fight
    if(sawWeakFriend && last < BOARD_CELLS) {
        uint8_t px = pos % BOARD_SIZE, py = pos / BOARD_SIZE;
        int8_t ux = px - last % BOARD_SIZE; if(ux < 0) ux = -ux;
        int8_t uy = py - last / BOARD_SIZE; if(uy < 0) uy = -uy;
        if(ux <= 2 && uy <= 2) bonus += PRIOR_URGENT;
    }

    // Thin-stretch penalty: tentatively place the stone and count the
    // merged group's liberties. Tactical moves are exempt (a capture
    // would raise the count anyway; a crosscut fight is legitimately
    // sharp). Patterned blocks at 2 libs still net above quiet moves;
    // outright self-atari sinks far below anything playable.
    if(!sawCapture && !sawSave && !sawAtari && !vitalHere) {
        simBoard[pos] = toMove;
        uint8_t a, b;
        uint8_t libs = groupLibsFind(pos, &a, &b);
        simBoard[pos] = EMPTY;
        if(libs <= 2)
            bonus -= (libs <= 1) ? PRIOR_SELFATARI_PENALTY
                                 : PRIOR_THIN_PENALTY;
    }

    // Center preference: the edge is worth less than the third line
    uint8_t x = pos % BOARD_SIZE, y = pos / BOARD_SIZE;
    uint8_t ex = x < BOARD_SIZE - 1 - x ? x : BOARD_SIZE - 1 - x;
    uint8_t ey = y < BOARD_SIZE - 1 - y ? y : BOARD_SIZE - 1 - y;
    uint8_t ed = ex < ey ? ex : ey;
    bonus += ed > PRIOR_CENTER_MAX ? PRIOR_CENTER_MAX : ed;
    if(triHere && ed <= 2) bonus--; // edge-facing triangle: worse still

    // Opening knowledge: an untouched corner is the biggest thing on
    // the board — steer toward its classic points (3-3/3-4/4-4
    // cluster). Self-limiting: the bonus dies as corners get stones.
    if(ex >= 2 && ex <= 3 && ey >= 2 && ey <= 3) {
        uint8_t cq = (y <= 3 ? 0 : 2) + (x <= 3 ? 0 : 1);
        if(emptyCorners & (1 << cq)) bonus += PRIOR_OPEN_CORNER;
    }

    // Cuttable-keima check. The knight's-move partner lies OUTSIDE the
    // candidate's 3x3, so neither patterns nor the thin-stretch check
    // can see this shape. Applies only when the keima is the sole link
    // (no orthogonal contact — checked above — and no diagonal one).
    if(!sawCapture && !sawSave && !sawAtari && !hasOrthFriend) {
        uint8_t hasDiagFriend = 0;
        for(int8_t dy = -1; dy <= 1; dy += 2)
            for(int8_t dx = -1; dx <= 1; dx += 2) {
                int8_t fx = x + dx, fy = y + dy;
                if(fx < 0 || fx >= BOARD_SIZE || fy < 0 || fy >= BOARD_SIZE)
                    continue;
                if(simBoard[fy * BOARD_SIZE + fx] == toMove)
                    hasDiagFriend = 1;
            }
        if(!hasDiagFriend) {
            uint8_t penalized = 0;
            for(int8_t a = -2; a <= 2 && !penalized; a++) {
                for(int8_t b = -2; b <= 2 && !penalized; b++) {
                    if(a * a + b * b != 5) continue; // knight offsets only
                    int8_t kx = x + a, ky = y + b;
                    if(kx < 0 || kx >= BOARD_SIZE ||
                       ky < 0 || ky >= BOARD_SIZE) continue;
                    if(simBoard[ky * BOARD_SIZE + kx] != toMove) continue;
                    // The two waist points between candidate and partner
                    int8_t w1x, w1y, w2x, w2y;
                    if(a == 1 || a == -1) { // |b| == 2
                        w1x = x;     w1y = y + b / 2;
                        w2x = x + a; w2y = y + b / 2;
                    } else {                // |a| == 2
                        w1x = x + a / 2; w1y = y;
                        w2x = x + a / 2; w2y = y + b;
                    }
                    // Enemy on or around a waist = a supported cut
                    if(oppNear(w1x, w1y, opp) || oppNear(w2x, w2y, opp)) {
                        bonus -= PRIOR_KEIMA_PENALTY;
                        penalized = 1;
                    }
                }
            }
        }
    }

    // Low-line discipline (see the defines): opening low-line moves
    // are penalized unconditionally; later, second-line moves near an
    // enemy stone are legitimate boundary plays. Penalized moves also
    // lose their shape/locality bonuses: a correct-LOOKING contact
    // answer down there is still usually wrong, and pattern+local
    // (+5) was overpowering the line penalty.
    uint8_t lowLineBad = 0;
    if(ed <= 1 && !sawCapture && !sawSave && !sawAtari && !vitalHere) {
        uint8_t nearEnemy = 0;
        if(ed == 1 && rootStones >= EARLY_STONES) {
            for(int8_t dy = -2; dy <= 2 && !nearEnemy; dy++)
                for(int8_t dx = -2; dx <= 2; dx++) {
                    int8_t nx = x + dx, ny = y + dy;
                    if(nx < 0 || nx >= BOARD_SIZE ||
                       ny < 0 || ny >= BOARD_SIZE) continue;
                    if(simBoard[ny * BOARD_SIZE + nx] == opp) {
                        nearEnemy = 1;
                        break;
                    }
                }
        }
        if(!nearEnemy) {
            lowLineBad = 1;
            bonus -= (ed == 0) ? PRIOR_EDGE_PENALTY : PRIOR_LINE2_PENALTY;
        }
    }

    // Locality: adjacent or diagonal to the previous move. Empty-
    // triangle candidates keep proximity credit ONLY when they touch
    // the pusher orthogonally — a true contact block is classically
    // excused its shape (and our block tests live there) — but a
    // merely diagonal-near triangle is a self-inflicted wound with
    // no answering duty, and with the credit the ugly connector of
    // a diagonal pair beat the good one 7/10.
    if(!lowLineBad && last < BOARD_CELLS) {
        int8_t dx = x - last % BOARD_SIZE; if(dx < 0) dx = -dx;
        int8_t dy = y - last / BOARD_SIZE; if(dy < 0) dy = -dy;
        if(dx <= 1 && dy <= 1 && (!triHere || dx + dy == 1)) {
            bonus += PRIOR_LOCAL;
            // Contact-push block (see PRIOR_BLOCK): their stone
            // touches us, this candidate touches the pusher
            // ORTHOGONALLY (the junction does; diagonal near-misses
            // scored alike in a real cut-through game until this)
            if(hasOrthFriend && dx + dy == 1) {
                uint8_t nbp[4];
                uint8_t np = neighbors(last, nbp);
                for(uint8_t j = 0; j < np; j++)
                    if(simBoard[nbp[j]] == toMove) {
                        bonus += PRIOR_BLOCK;
                        break;
                    }
            }
        }
    }

    // Local shape: the same 3x3 patterns the playouts use. This is what
    // makes cut-defense (blocking a keima push, connecting a jump)
    // visible to the tree instead of only to the rollouts.
    if(!lowLineBad && patternMatch(x, y, toMove)) bonus += PRIOR_PATTERN;

    // Big open point: the territory-staking move
    if(isFar) bonus += PRIOR_BIG;

    return bonus;
}

// Push-front: child order carries no meaning beyond tie-breaking, and
// random scan starts keep the ties fair. A negative bonus becomes
// virtual losses: extra visits with no wins.
static uint8_t addChild(uint8_t nodeIdx, uint8_t move, int8_t bonus) {
    uint8_t c = newNode(move);
    if(c == 0xFF) return 0xFF;
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

// Root expansion, PROGRESSIVE (see ROOT_INIT): pass plus the top
// prior candidates; the rest arrive via widening as visits grow, so
// early visits concentrate instead of spreading over everything.
static uint8_t widenNode(uint8_t nodeIdx, uint8_t toMove, uint8_t ko, uint8_t last);

static void expandNode(uint8_t nodeIdx, uint8_t toMove, uint8_t ko, uint8_t last) {
    if(poolUsed < NODE_POOL) {
        uint8_t c = addChild(nodeIdx, MOVE_PASS, 0);
        nSetStats(c, PRIOR_BASE_V, 0); // passing is a last resort
    }
    for(uint8_t k = 0; k < ROOT_INIT; k++)
        if(!widenNode(nodeIdx, toMove, ko, last)) break;
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
    buildChainMap();

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
    return addChild(nodeIdx, bestPos, bestP) != 0xFF;
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

    uint8_t atRoot = (nodeIdx == 0);
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

        // Root RAVE blend, Gelly-Silver β from the CHILD's visits:
        // fresh children lean on AMAF evidence, established children
        // graduate to their own record. (β from the parent's count
        // kept even a 130-visit leader half-AMAF and flattened the
        // root.) Never lift a poisoned (illegal) child back via RAVE.
        if(atRoot && nv < POISONED &&
           n.move < BOARD_CELLS && raveV[n.move]) {
            uint16_t ratio = ((uint32_t)RAVE_K << 12) /
                             (3 * nv + RAVE_K);
            uint16_t beta = isqrt32((uint32_t)ratio << 12);
            uint16_t qr = ((uint32_t)raveW[n.move] << 12) / raveV[n.move];
            q = ((uint32_t)(4096 - beta) * q + (uint32_t)beta * qr) >> 12;
        }

        uint16_t u = q + (isqrt32((uint32_t)lnOverN * v) >> UCB_EXPLORE_SHIFT);
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
    cacheLibsPos = 0xFF; // fresh position: liberty cache is stale
    uint8_t ko = rootKo;
    uint8_t toMove = rootTurn;
    uint8_t lastMove = rootLast;

    pathDepth = 0;
    uint8_t cur = 0;
    path[pathDepth++] = 0;
    uint8_t retries = 0;

    // Selection + expansion: descend the existing tree, add at most
    // ONE new node per iteration, then playout. (Chaining expansions
    // to full depth here would burn the whole pool on the first few
    // prior-guided noodles and freeze the tree for the rest of the
    // search.)
    while(pathDepth < 31) {
        uint8_t fresh = 0;
        if(node(cur).firstChild == 0xFF) {
            // Cold leaves just playout; see EXPAND_VISITS
            if(cur != 0 && nVisits(cur) < EXPAND_VISITS) break;
            if(allocReady()) {
                if(cur == 0) expandNode(cur, toMove, ko, lastMove);
                else fresh = widenNode(cur, toMove, ko, lastMove);
            }
            if(node(cur).firstChild == 0xFF) break; // terminal or pool dry
        } else {
            // Progressive widening: one more candidate as visits grow
            // (the root has its own, wider schedule)
            uint8_t maxKids;
            if(cur == 0) {
                uint16_t mk = ROOT_INIT + nVisits(0) / ROOT_WIDEN_RATE;
                maxKids = mk > 80 ? 80 : (uint8_t)mk;
            } else {
                maxKids = 1 + nVisits(cur) / WIDEN_RATE;
                if(maxKids > WIDEN_CAP) maxKids = WIDEN_CAP;
            }
            if(childCount(cur) < maxKids && allocReady())
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
        path[pathDepth++] = c;
        if(fresh) break; // the freshly expanded child: playout from here
    }

    uint8_t winner = playout(toMove, ko, lastMove);

    // Fold this simulation into the root RAVE tables
    uint8_t rootWin = (winner == rootTurn);
    thinkSims++;
    if(rootWin) thinkSimWins++;
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
    for(uint8_t i = 0; i < pathDepth; i++) {
        uint8_t win = 0;
        if(i > 0) {
            uint8_t mover = (i & 1) ? rootTurn : 3 - rootTurn;
            win = (mover == winner);
        }
        nBump(path[i], win);
    }
}

void AI::think(Game &game) {
    // Opponent just passed: passing back ends the game right now, so
    // if the game as it stands is already won, take it — no search.
    // Dead enemy stones make computeScore undercount our territory,
    // which only delays this trigger until they are actually captured
    // (it can never pass into a loss by the game's own scoring).
    passToWin = 0;
    resigned = 0;
    if(game.consecutivePasses == 1) {
        // Don't trust the count if the previous search already read
        // this game as bad (under ~30%): dead stones make
        // computeScore miscount in both directions, and a massacre
        // position full of our corpses can neutralize enough enemy
        // territory to read as a "win" — passing then gifts the game.
        uint8_t evalOK = !thinkSims ||
            (uint32_t)thinkSimWins * 10 >= (uint32_t)thinkSims * 3;
        if(evalOK) {
            game.computeScore();
            if(game.winner() == game.turn) {
                passToWin = 1;
                resignCount = 0;
                return;
            }
        }
    }

    poolUsed = 0;
    freeHead = 0xFF;
    thinkSims = thinkSimWins = 0;
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

    // Scan the root board for eyespace vital points (see rootVitals)
    for(uint8_t i = 0; i < BOARD_CELLS; i++)
        simBoard[i] = packedGet(game.board, i);
    nRootVitals = 0;
    for(uint8_t i = 0; i < BOARD_CELLS && nRootVitals < 3; i++) {
        if(simBoard[i] != EMPTY) continue;
        uint8_t vital;
        if(regionVital(i, &vital) && vital == i)
            rootVitals[nRootVitals++] = i;
    }

    memset(raveV, 0, BOARD_CELLS);
    memset(raveW, 0, BOARD_CELLS);

    newNode(0xFF); // root
    uint16_t iters = mctsIterations;
    if(rootStones < OPENING_BOOST_STONES) iters += iters / 2;
    uint16_t total = iters;
    uint8_t extended = 0;
    for(uint16_t i = 0; i < total; i++) {
        mctsIterate(game);
        // Flat-root check at budget end (see UNCERTAIN_MIN)
        if(i + 1 == total && !extended) {
            extended = 1;
            uint16_t lead = 0;
            for(uint8_t c = node(0).firstChild; c != 0xFF;
                c = node(c).nextSibling) {
                uint16_t v = nVisits(c);
                if(v < POISONED && v > lead) lead = v;
            }
            if(lead < UNCERTAIN_MIN) total += iters / 2;
        }

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
            if(top1 - top2 > total - 1 - i) break;
        }
    }

    // Resignation check (see RESIGN_* above), two tiers
    if(rootStones >= RESIGN_MIN_STONES &&
       thinkSimWins * RESIGN_DENOM < thinkSims)
        resignCount++;
    else
        resignCount = 0;
    if(rootStones >= RESIGN_MIN_STONES &&
       thinkSimWins * RESIGN2_DENOM < thinkSims)
        resignCount2++;
    else
        resignCount2 = 0;
    resigned = (resignCount >= resignStreak) ||
               (resignCount2 >= RESIGN2_STREAK);
}

uint8_t AI::bestMove(Game &game, uint8_t &x, uint8_t &y) {
    if(passToWin) return 0; // ending the game now wins it

    // Root move by highest LOWER confidence bound on the win rate
    // (Leela-style): lcb = q - z*sqrt(q(1-q)/n), Q12 fixed point.
    // Beats most-visited when a visit-leader's win rate is decaying.
    // Low-visit children punish themselves via the wide bound, and the
    // real-rules validation (full ko) stays lazy: an invalid favorite
    // falls through to the next-best instead of turning into a pass.
    int16_t bestL = -32768;
    uint16_t backV = 0, maxV = 0;
    uint8_t best = MOVE_PASS, backup = MOVE_PASS;
    for(uint8_t c = node(0).firstChild; c != 0xFF; c = node(c).nextSibling) {
        uint16_t v = nVisits(c);
        if(v < POISONED && v > maxV) maxV = v;
    }
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
        if(v < LCB_GATE || v * LCB_REL_DIV < maxV) continue;

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

    // The search's favorite landing inside settled territory — ours
    // or theirs — means nothing meaningful is left on the board: an
    // own fill loses a point, a hopeless invasion gifts a prisoner.
    // Pass, but only while WINNING by the current count: the detector
    // cannot tell settled territory from a living group's own
    // eyespace, and a forced pass must never deny a losing position
    // its defensive tries. (Hopeless games are handled by resignation
    // now; real threats also re-enable moves automatically, since an
    // invaded region touches both colors and stops counting as
    // settled.)
    for(uint8_t i = 0; i < BOARD_CELLS; i++)
        simBoard[i] = packedGet(game.board, i);
    uint8_t vital;
    if(regionVital(best, &vital) && vital != best) {
        game.computeScore();
        if(game.winner() == game.turn) return 0;
    }

    x = best % BOARD_SIZE;
    y = best / BOARD_SIZE;
    return 1;
}
