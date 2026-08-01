#include "ai.h"
#include "opening_book.h"
#include "neighbor_table.h"

// Random draws: the device uses the engine's own xorshift (seeded at
// boot) so libc random() never links; the host keeps libc random()
// for per-game srand determinism in the harness. (see rngState)
static uint16_t rnd16();
static uint8_t rnd(uint8_t n);
#ifdef ARDUINO
#define SYS_RND(n)   rnd((uint8_t)(n))
#define SYS_RNDW(n)  (int16_t)(rnd16() % (uint16_t)(n))
#else
#define SYS_RND(n)   random(n)
#define SYS_RNDW(n)  random(n)
#endif

// Dynamic-komi state for graceful losing — adapted at the end of
// each think, applied to playout scoring (see scoreWinner /
// vKomiWinner / the adaptation block in think).
#ifndef VKOMI_STEP2
#define VKOMI_STEP2 4   // adaptation step, half-points (= 2 points)
#endif
#ifndef VKOMI_MAX2
#define VKOMI_MAX2 24   // spot at most 12 points before giving up
#endif
static uint8_t vKomi2;

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
    vKomi2 = 0;
}

// ==================== v2 bitstream walker ====================
// Format: see opening_book.h. LSB-first bit stream; a node's children
// follow its header; every group's first child is absolute (IDX[5]
// LEAF LAST) and the rest are gap-coded tails sorted ascending by table
// index (match-only, so the order is free). The root group is
// all-absolute (its order carries OPENING_ROOT_WEIGHTS).
//
// The walk is forward-only, so a single global cursor (bkPos) + flag
// statics replace out-parameters -- pointer plumbing tripled the code
// size at -Os.
static uint16_t bkPos;   // bit cursor
static uint8_t bkFlags;  // node just decoded: bit0 = leaf, bit1 = last

__attribute__((noinline))
static uint8_t bookReadBits(uint16_t p, uint8_t mask) {
    // up to 8 bits (mask = (1<<n)-1, constant at every call site); the
    // stream carries a pad byte so the 2-byte window never over-reads
    uint16_t byte = p >> 3;
    uint8_t sh = p & 7;
    uint16_t w = pgm_read_byte(OPENING_BOOK_TRIE + byte) |
                 ((uint16_t)pgm_read_byte(OPENING_BOOK_TRIE + byte + 1) << 8);
    return (uint8_t)(w >> sh) & mask;
}

__attribute__((noinline))
static uint8_t bkAbs(void) {              // IDX[5] LEAF LAST, one read
    uint8_t v = bookReadBits(bkPos, 0x7F);
    bkPos += 7;
    bkFlags = v >> 5;
    return v & 31;
}

__attribute__((noinline))
static uint8_t bkGap(void) {
    // branches only compute (gap, flag bits, advance); the stores and the
    // 16-bit cursor update live in ONE shared tail -- gcc duplicated them
    // per branch otherwise (~34 B each)
    uint8_t v = bookReadBits(bkPos, 0xFF);
    uint8_t g, f, adv;
    if(!(v & 1))      { g = 1;                  f = v >> 1; adv = 3; }
    else if(!(v & 2)) { g = 2 + ((v >> 2) & 1); f = v >> 3; adv = 5; }
    else if(!(v & 4)) { g = 4 + ((v >> 3) & 3); f = v >> 5; adv = 7; }
    else {                       // 8-bit escape: flags in a second read
        g = 8 + (v >> 3);
        bkPos += 8;
        f = bookReadBits(bkPos, 0x03);
        adv = 2;
    }
    bkFlags = f & 3;
    bkPos += adv;
    return g;
}


static void bkSkipGroup(void) {           // cursor at first child -> past group
    bkAbs();
    uint8_t f = bkFlags;
    if(!(f & 1)) bkSkipGroup();
    while(!(f & 2)) {
        bkGap();
        f = bkFlags;
        if(!(f & 1)) bkSkipGroup();
    }
}

static uint8_t bookPointIdx(uint8_t mv) { // 7x7 move -> table index
    for(uint8_t i = 0; i < 32; i++)
        if(pgm_read_byte(BOOK_POINTS + i) == mv) return i;
    return 0xFF;
}

// Scan the group at bkPos for table-idx t. On hit returns 1 with bkPos
// just past the node header (= its child group when the node is not a
// leaf).
static uint8_t bookFindChild(uint8_t t) {
    uint8_t idx = bkAbs();
    uint8_t f = bkFlags;
    if(idx == t) return 1;
    if(!(f & 1)) bkSkipGroup();
    int8_t run = -1;
    while(!(f & 2)) {
        run += bkGap();
        idx = (uint8_t)run;
        f = bkFlags;
        if(idx == t) return 1;
        if(!(f & 1)) bkSkipGroup();
    }
    return 0;
}

void AI::notifyMove(uint8_t x, uint8_t y) {
    firstMove = 0;

    // Book never contains first-line moves
    if(x == 0 || x == BOARD_SIZE - 1 || y == 0 || y == BOARD_SIZE - 1) {
        bookAlive = 0;
        return;
    }

    uint8_t m = 1;
    for(uint8_t s = 0; s < 8; s++, m <<= 1) {
        // walking mask: (1 << s) compiles to a variable shift LOOP at -Os
        if(!(bookAlive & m)) continue;

        uint8_t bx = x, by = y;
        applySym(bx, by, s);
        uint8_t t = bookPointIdx((by - 1) * 7 + (bx - 1));
        bkPos = bookPos[s];
        if(t == 0xFF || !bookFindChild(t) || (bkFlags & 1)) {
            bookAlive &= ~m;
            continue;
        }
        bookPos[s] = bkPos; // children start right after the node header
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

        int16_t r = SYS_RNDW(total);
        bkPos = 0;
        uint8_t idx = 0;
        int8_t run = -1;
        for(uint8_t i = 0; ; i++) {
            if(i == 0) idx = bkAbs();
            else { run += bkGap(); idx = (uint8_t)run; }
            r -= pgm_read_byte(OPENING_ROOT_WEIGHTS + i);
            if(r < 0) break;
            if(!(bkFlags & 1)) bkSkipGroup();
        }

        uint8_t mv = pgm_read_byte(BOOK_POINTS + idx);
        x = mv % 7 + 1;
        y = mv / 7 + 1;
        applyInvSym(x, y, SYS_RND(8));
        return 1;
    }

    if(!bookAlive) return 0;

    uint8_t m = 1;
    for(uint8_t s = 0; s < 8; s++, m <<= 1) {
        if(!(bookAlive & m)) continue;
        // First child = highest-policy move (absolute header)
        uint8_t idx = bookReadBits(bookPos[s], 0x1F);
        uint8_t mv = pgm_read_byte(BOOK_POINTS + idx);
        x = mv % 7 + 1;
        y = mv / 7 + 1;
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

// Connection. The WEAK form fires when the weakest friendly chain
// involved has exactly 2 liberties — real danger of being split off
// (1-liberty chains are handled by the ladder-verified save). The
// plain PRIOR_CONNECT fires earlier: mid-game, when the candidate is
// the SOLE connector of two chains (see soleConnector) — defend the
// cutting point BEFORE the cut. The miai test is what keeps this from
// over-connecting (a generic two-chain bonus measurably tanked).
#ifndef PRIOR_CONNECT
#define PRIOR_CONNECT 4
#endif
#ifndef PRIOR_CONNECT_WEAK
#define PRIOR_CONNECT_WEAK 6
#endif

// A candidate whose ONLY link to friendly stones is a knight's move,
// with enemy support around the waist points, is an extension that
// can be pushed through and cut immediately.
#ifndef PRIOR_KEIMA_PENALTY
#define PRIOR_KEIMA_PENALTY 4
#endif
// Cuttable one-point jump (ikken tobi): candidate jumps to a lone
// partner two straight away, the midpoint is EMPTY and an enemy stone
// sits BESIDE it (push-and-cut ready). Merged into the keima scan:
// same lone-stone gates; !hasDiagFriend already excludes side-supported
// jumps (the midpoint's sides are the candidate's diagonals).
#ifndef PRIOR_JUMP_PENALTY
#define PRIOR_JUMP_PENALTY 4
#endif

// Playout throw-in exception (the kill-bias fix): normal playouts gate
// self-atari off entirely, so they can NEVER play the sacrifice that
// kills an eyespaced-but-dead group -- dead groups survived every
// rollout and the eval read lost positions at ~50-65% (measured: 66%
// win-rate on a true -16.5 board; scoreDead's self-atari-allowed vote
// reads the same board correctly). A LONE stone in self-atari is by
// construction a throw-in (no friendly neighbour + one liberty => all
// non-empty neighbours are enemy); allow exactly that shape, at rate
// 1/PLAYOUT_THROWIN_RATE (power of 2; 1 = always, 0 = off). Connected
// self-atari -- feeding a group into atari -- stays rejected.
#ifndef PLAYOUT_THROWIN_RATE
#define PLAYOUT_THROWIN_RATE 1
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

// Settle gate (see settleVote): after the opponent passes in the
// endgame, accept the honest count and pass back once the ownership
// vote reads us behind by at least this many HALF-POINTS-doubled
// (6 = 3 points). Between winning and this, keep playing the close
// ones out.
#ifndef SETTLE_ACCEPT2
#define SETTLE_ACCEPT2 6
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
// Candidates added per widenNode scan (see the batch comment there)
#ifndef WIDEN_BATCH
#define WIDEN_BATCH 3
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
// Contact-push answer probability mask (3 = 3/4 of the non-local
// coin, stacking with the local-answer step to ~7/8; 0 disables).
#ifndef CONTACT_ANSWER_MASK
#define CONTACT_ANSWER_MASK 3
#endif
// Lone-invader answer probability mask, SEPARATE from the contact
// answer so the invasion knob can be tuned in isolation (the two were
// one gate; a sweep of the combined mask confounded boundary defense
// with invasion optimism).
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

// Single-bit masks: `1 << (i & 7)` compiles to a variable shift LOOP at
// -Os (ror/dec/brpl, ~2-9 cycles) at every bitmap site; an lpm from this
// table is a constant 3 cycles. Same values -- bit-identical results.
PROGMEM const uint8_t BIT_MASK[8] = {1, 2, 4, 8, 16, 32, 64, 128};
static inline uint8_t bitMask(uint8_t i) {
    return pgm_read_byte(BIT_MASK + (i & 7));
}

// Which points the root player touched in the current simulation
static uint8_t raveMask[11];

static inline void raveMark(uint8_t pos) {
    raveMask[pos >> 3] |= bitMask(pos);
}

// Static pool extension
static Node poolExt[NODE_POOL_EXT];

// Plain pool indexing — no 0x800 redirect. The magic-key bytes at
// RAM 0x800-0x801 land in the RAVE tables above (harmless statistics);
// test/checkmagic.sh asserts at build time that neither node region
// (pool[0..142], poolExt) nor floodScratch spans 0x800, failing the
// build loudly if a RAM-layout change ever pushes them onto it. That
// replaced the old per-access redirect, which cost ~3% of think time
// (floodSlot alone, by the emulator profile) guarding a collision the
// layout already prevents.
static inline Node& node(uint8_t i) {
    return (i < NODE_POOL_SB) ? pool[i] : poolExt[i - NODE_POOL_SB];
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
#ifndef ARDUINO
// Host diagnostic: mean root-relative TRUE margin (doubled points, komi
// applied) across this think's playouts, annotated by play_gui as est=.
// NOT a trustworthy lost-position detector: normal playouts run with
// scoreMode=0 (self-atari gated off), so they can never play the
// throw-in sacrifices that kill an eyespaced-but-dead group — the
// margin inherits that systematic bias wholesale (measured on the
// Jul 30 SGF: est +4.5 at a true −16.5, win-rate just as fooled).
// The only honest ownership read is the scoreMode=1 vote (settleVote /
// scoreDead). Kept as an SGF red-flag diagnostic: est far from the
// eventual scoreDead count marks positions the playout policy misreads.
static int32_t thinkMargin2Sum;
int16_t thinkAvgMargin2;
// Wait accounting for the FASTPLAY experiment: iterations actually
// run vs budgeted, across all thinks (host tools print the ratio)
uint32_t thinkItersRun, thinkItersBudget;
#endif
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
// Which empty cells' eyespace code (chainId bits 6-7) is cached this
// widen (see buildChainMap / regionVital's lazy stamp). 81 bits.
static uint8_t regionDone[11];
#define CHAIN_OF(b) ((b) & 0x3F)
#define LIBS_OF(b) ((b) >> 6)
static uint8_t markEpoch;
// Set only inside scoreDead's vote playouts (see playoutTry)
static uint8_t scoreMode;
#ifdef PLAYOUT_STATS
uint32_t spLoneOK, spLoneRej, spFloodOK, spFloodRej;
#endif

// xorshift16 state; 0 is sticky, so every seeding path must |1.
// On the DEVICE this is seeded once at boot (see setup()) and
// free-runs — routing every random draw through it lets the AVR
// build drop libc random_r (~400 bytes of flash). The host harness
// keeps libc random() so per-game srand determinism is unchanged.
uint16_t rngState = 1;
#ifndef ARDUINO
// Host-only debug hooks for reproducibility (play_gui SGF annotations).
// think() records the seed it started from in lastThinkSeed; setting
// forceThinkSeed nonzero replays that exact seed. Compiled out on device.
uint16_t lastThinkSeed = 0, forceThinkSeed = 0;
#endif
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
    // Lemire multiplicative reduction: map rnd16()'s [0,2^16) uniformly into
    // [0,n) with a 16x8 multiply + high-word take, instead of a ~180-cycle
    // variable __udivmodhi4. NOT bit-identical to %n (a different draw->index
    // map) but the same uniform distribution -- strength validated vs L0.
    return ((uint32_t)rnd16() * n) >> 16;
}

// rnd() for a COMPILE-TIME modulus. At -Os gcc emits a small rcall
// __udivmodhi4 even for a constant divisor; forcing -O2 (and out-of-line so
// the attribute sticks through inlining) turns `% N` into a magic-multiply.
// Bit-identical to rnd(N) -- same rnd16() draw, same remainder.
template<uint8_t N>
__attribute__((optimize("O2"), noinline))
static uint8_t rndMod() { return rnd16() % N; }

// Fused PROGMEM read + post-increment: avr-libc's pgm_read_byte(p++) emits
// `lpm; adiw` (read then a separate 2-cyc increment); `lpm Z+` does both in
// one 3-cyc instruction. Provably identical (read *p, p++); host uses the
// portable form so movecmp still verifies the algorithm.
static inline uint8_t lpmNext(const uint8_t *&p) {
    uint8_t v;
#if defined(__AVR__)
    asm("lpm %0, Z+" : "=r"(v), "=z"(p) : "1"(p));
#else
    v = pgm_read_byte(p++);
#endif
    return v;
}
// Iterate the 0xFF-terminated neighbour list of cell `pos`; `q` (a uint8_t
// you declare) takes each neighbour in turn -- no count, no nb[] round-trip.
#define FOR_EACH_NEIGHBOR(q, pos) \
    for(const uint8_t *_ne = NEIGHBOR_TABLE + (pos) * 5; \
        ((q) = lpmNext(_ne)) != 0xFF; )

static void newMark() {
    if(++markEpoch == 0) {
        memset(simMark, 0, sizeof(simMark));
        markEpoch = 1;
    }
}

// Board index -> packed (y<<4)|x. AVR has no hardware divide, so the
// pervasive pos%9 / pos/9 split compiled to __udivmodqi4, a ~40-cycle
// bit loop. The hardware multiplier gives the result in a few cycles:
// y=(pos*57)>>9 is exact for every pos in 0..80 (checked to 0..255),
// and x = pos - y*9. Applied to the hot playout/prior/widen sites this
// is play-identical (verified bit-for-bit) for ~0.4% off the whole
// search — small because the remaining divide time is the 16/32-bit
// UCB win-rate divides, which have variable divisors and stay.
static uint8_t posXY(uint8_t pos) {
    uint8_t y = ((uint16_t)pos * 57) >> 9;
    return (uint8_t)(y << 4) | (uint8_t)(pos - y * 9);
}

static uint32_t groupLibsCore(uint8_t start, uint8_t markAll, uint8_t cap);

// Does the group at start have any liberty? Dedicated flood: unlike the
// shared core it keeps no liberty list, count, or dedup — it just bails
// on the first empty cell it sees. This is the hottest liberty query
// (simPlay runs it per opponent neighbor and per ungated move), so the
// leaner inner loop earns back its own flash. Bit-identical: a boolean
// "any liberty" is independent of flood order.
// On a full flood (return 0 = no liberty = captured), hasLiberty leaves the
// whole group in floodScratch[0..capturedGroupN-1] so removeGroup can sweep
// it instead of re-flooding (its only caller runs it right after).
static uint8_t capturedGroupN;

__attribute__((optimize("O2")))
static uint8_t hasLiberty(uint8_t start) {
    uint8_t color = simBoard[start];
    newMark();
    // BFS with chasing read/write indices into floodScratch: stones are
    // appended (wr) and consumed front-to-back (rd), so the group accumulates
    // append-only and is still there on a full flood. Done when rd meets wr.
    uint8_t rd = 0, wr = 1;
    floodSlot(0) = start;
    simMark[start] = markEpoch;
    do {
        uint8_t p = floodSlot(rd++);
        // neighbors() inlined and fused: walk the 0xFF-terminated neighbour
        // list straight from PROGMEM (lpm Z+). An empty neighbour = a
        // liberty; exit before reading the rest.
        const uint8_t *e = NEIGHBOR_TABLE + p * 5;
        uint8_t q;
        while((q = lpmNext(e)) != 0xFF) {
            if(simBoard[q] == EMPTY) return 1;
            if(simBoard[q] == color && simMark[q] != markEpoch) {
                simMark[q] = markEpoch;
                floodSlot(wr++) = q;
            }
        }
    } while(rd != wr);
    capturedGroupN = wr;   // whole group left in floodScratch[0..wr-1]
    return 0;
}

// Remove the group hasLiberty just found to be captured. It left the whole
// group in floodScratch[0..capturedGroupN-1] (its sole caller, simPlay, runs
// hasLiberty(start)==0 immediately before this), so sweep the list -- no
// second flood. INVARIANT: only valid right after hasLiberty(start) == 0.
static uint8_t removeGroup(uint8_t start) {
    (void)start;
    uint8_t n = capturedGroupN;
    for(uint8_t i = 0; i < n; i++) simBoard[floodSlot(i)] = EMPTY;
    return n;
}

static uint32_t groupLibsFind(uint8_t start);

// If the group at start has exactly one liberty, return it; else 0xFF.
// (Thin wrapper: groupLibsFind's early-exit-at-3 does the same flood
// with marginally more work than a dedicated exit-at-2 — the ~150
// bytes of flash matter more than those cycles.)
static uint8_t soleLiberty(uint8_t start) {
    uint32_t r = groupLibsCore(start, 0, 2);
    return (uint8_t)r == 1 ? (uint8_t)(r >> 8) : 0xFF;
}

// Shared flood core for ALL the liberty finders: counts distinct
// liberties, early-exiting once `cap` are found (1 = hasLiberty,
// 2 = soleLiberty, 3 = full find — profiling showed hasLiberty
// routed through a fixed cap-3 version was HALF of all search time).
// With markAll it floods the WHOLE group into the CURRENT mark epoch
// (membership tests need complete marking; cap is ignored).
// Returns count | (l1<<8) | (l2<<16), the packed-return idiom from
// playoutTry scaled to the flood core: two pointer out-params became
// register byte-extracts at every caller (the out-param tax scales
// with call count, and this is a six-figure-calls path).
static uint32_t groupLibsCore(uint8_t start, uint8_t markAll, uint8_t cap) {
    uint8_t color = simBoard[start];
    uint8_t lib1 = 0xFF, lib2 = 0xFF;
    uint8_t count = 0;
    uint8_t sp = 0;

    if(!markAll) {
        // Seed fast path: scan the seed's neighbours once -- count its
        // empty liberties and push its same-colour neighbours STRAIGHT
        // onto the flood stack (no same[4] buffer: the entries are dead
        // scratch if enough empties settle it early, and pushing here
        // keeps table order for the flood). Enough empties settles it
        // before any flood setup (the common case); otherwise marks are
        // stamped from the stack once newMark has run, and the flood
        // continues from those neighbours -- the seed is never
        // re-scanned.
        uint8_t q;
        FOR_EACH_NEIGHBOR(q, start) {
            uint8_t s = simBoard[q];
            if(s == EMPTY) {
                if(lib1 == 0xFF) lib1 = q;
                else if(lib2 == 0xFF) lib2 = q;
                if(++count >= cap)
                    return (uint32_t)count | ((uint16_t)lib1 << 8) |
                           ((uint32_t)lib2 << 16);
            } else if(s == color) {
                floodSlot(sp++) = q;
            }
        }
        newMark();
        simMark[start] = markEpoch;
        for(uint8_t k = 0; k < sp; k++)
            simMark[floodSlot(k)] = markEpoch;
    } else {
        // markAll floods into the CURRENT epoch (see header) -- no newMark
        floodSlot(sp++) = start;
        simMark[start] = markEpoch;
    }

    while(sp && (markAll || count < cap)) {
        uint8_t p = floodSlot(--sp);
        uint8_t q;
        FOR_EACH_NEIGHBOR(q, p) {
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
    return (uint32_t)count | ((uint16_t)lib1 << 8) |
           ((uint32_t)lib2 << 16);
}

// Find a group's distinct liberties, early-exiting at 3. Fills l1/l2
// with the first two (0xFF if fewer). Returns the count, 0-3.
static uint32_t groupLibsFind(uint8_t start) {
    return groupLibsCore(start, 0, 3);
}

static uint8_t groupLibsMax3(uint8_t start) {
    uint8_t n = (uint8_t)groupLibsFind(start);
    return n ? n : 1; // preserve old behavior: 0 liberties reads as 1
}

// Full-flood variant of groupLibsFind (see groupLibsCore's markAll)
static uint8_t groupLibsMark(uint8_t start) {
    return (uint8_t)groupLibsCore(start, 1, 3);
}

#define SETTLED_REGION_MAX 8
// Vital point of a small marked empty region: the UNIQUE cell of max
// degree within it (region[0..cnt-1], all in the current mark epoch),
// or 0xFF if none. Shared by regionVital and its lazy cache stamp;
// bestDeg/ties feed the square-four gate.
static uint8_t regionVitalCell(const uint8_t *region, uint8_t cnt,
                               uint8_t *bestDeg, uint8_t *ties) {
    uint8_t bd = 1, bc = 0xFF, t = 0;
    for(uint8_t j = 0; j < cnt; j++) {
        uint8_t deg = 0, q;
        FOR_EACH_NEIGHBOR(q, region[j])
            if(simBoard[q] == EMPTY && simMark[q] == markEpoch) deg++;
        if(deg > bd) { bd = deg; bc = region[j]; t = 1; }
        else if(deg == bd) t++;
    }
    *bestDeg = bd; *ties = t;
    return bc;
}
// Build the chain map: flood each chain to assign ids and count
// liberties, then a second cheap walk stamps the libs bits. A lazy
// eyespace map also lives in the empty cells' top bits, filled on
// demand by regionVital; buildChainMap just clears its cache flags.
static void buildChainMap() {
    memset(chainId, 0, sizeof(chainId));
    uint8_t nextId = 0;
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
            uint8_t q;
            FOR_EACH_NEIGHBOR(q, p) {
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
        // Stamp libs into the top bits of every member. The dedup below
        // relies on `chainId[q] |= bits` flipping q off the `== id` match;
        // with count==0 (a 0-liberty group, only reachable from an illegal
        // board) bits==0, so the OR is a no-op and the flood never
        // terminates. Nothing to stamp then anyway, so guard it.
        uint8_t bits = count << 6;
        if(bits) {
            sp = 0;
            floodSlot(sp++) = s;
            chainId[s] |= bits;
            while(sp) {
                uint8_t p = floodSlot(--sp);
                uint8_t q;
                FOR_EACH_NEIGHBOR(q, p) {
                    if(chainId[q] == id) { // id match, libs not yet stamped
                        chainId[q] |= bits;
                        floodSlot(sp++) = q;
                    }
                }
            }
        }
    }
    // Fresh eyespace cache for this node's board (see regionVital).
    memset(regionDone, 0, sizeof(regionDone));
}

// pos touches two DISTINCT friendly chains. Is it their ONLY connecting
// point? A connector is an empty cell adjacent to both chains, i.e. a
// liberty of chain A that also touches chain B — so flood A from a seed
// stone and check its empty neighbours, instead of rescanning the whole
// board. Same-colour distinct chains are never adjacent, so the flood
// stays within A. Exactly one connector (pos itself) means the opponent
// playing here splits us for real; two or more is miai (no urgency —
// bonusing those over-concentrated and measurably tanked an earlier
// connect prior).
static uint8_t soleConnector(uint8_t seedA, uint8_t idB) {
    uint8_t colorA = simBoard[seedA];
    uint8_t connectors = 0;
    newMark();
    uint8_t sp = 0;
    floodSlot(sp++) = seedA;
    simMark[seedA] = markEpoch;
    while(sp) {
        uint8_t p = floodSlot(--sp);
        uint8_t q;
        FOR_EACH_NEIGHBOR(q, p) {
            if(simMark[q] == markEpoch) continue;
            if(simBoard[q] == colorA) {         // extend the flood over chain A
                simMark[q] = markEpoch;
                floodSlot(sp++) = q;
            } else if(simBoard[q] == EMPTY) {   // a liberty of A — connector?
                simMark[q] = markEpoch;         // dedup this empty cell
                uint8_t r, nearB = 0;
                FOR_EACH_NEIGHBOR(r, q)
                    if(CHAIN_OF(chainId[r]) == idB) { nearB = 1; break; }
                if(nearB && ++connectors > 1) return 0;
            }
        }
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
// makes the classics come out right by itself — line four has no
// single deciding point and correctly yields none; square four
// (dead from any entry) is special-cased below.
// Whoever plays the vital point decides the region's life: the owner
// splits it into two eyes, the opponent reduces it to one.
//
// Non-vital moves inside a settled region are pointless-to-harmful
// under the Japanese rules the game scores by (own fill: -1 point;
// hopeless invasion: gifts a prisoner), but the area-scoring
// playouts think they are free.
// Returns (vital<<8) | ownerCode: low byte 0 = unsettled/open (as the
// old uint8_t return), high byte = the vital cell or 0xFF. Packed so no
// caller needs an out-param (the pointer plumbing showed in the
// profile; one hot caller only wants the cache side-effect).
static uint16_t regionVital(uint8_t seed) {
    uint8_t region[SETTLED_REGION_MAX];
    uint8_t cnt = 0, head = 0;
    uint8_t owner = 0, unsettled = 0;
    uint8_t vit = 0xFF;
    newMark();
    region[cnt++] = seed;
    simMark[seed] = markEpoch;
    while(head < cnt && !unsettled) {
        uint8_t q;
        FOR_EACH_NEIGHBOR(q, region[head++]) {
            uint8_t s = simBoard[q];
            if(s != EMPTY) {
                if(!owner) owner = s;
                else if(owner != s) { unsettled = 1; break; } // both colors
                continue;
            }
            if(simMark[q] == markEpoch) continue;
            if(cnt >= SETTLED_REGION_MAX) { unsettled = 1; break; } // too open
            simMark[q] = markEpoch;
            region[cnt++] = q;
        }
    }
    if(!unsettled && owner && cnt >= 3 && cnt <= 6) {
        uint8_t bestDeg, ties;
        uint8_t bestCell = regionVitalCell(region, cnt, &bestDeg, &ties);
        if(bestCell != 0xFF && ties == 1) vit = bestCell;
        // Square four: the one shape the unique-max rule misses.
        // All four cells tie at degree 2 (a straight four has two
        // degree-1 ends, so this signature is unambiguous). It is a
        // DEAD shape — any cell starts the kill, and what remains
        // is a bent three whose center the unique-max rule then
        // finds. SCORING ONLY: in live play a 2x2 pocket usually
        // belongs to a group with eyes elsewhere, and treating it
        // as vital invited gift-stone invasions and own-eye fills
        // (160-game referee dropped 26 -> 17 before this gate).
        else if(scoreMode && cnt == 4 && bestDeg == 2 && ties == 4)
            vit = bestCell;
    }
    // Lazy eyespace cache: stamp the flooded cells (<=8) with the region
    // code (bits 6-7: 1=black, 2=white, 0=open; the vital cell = 3) and
    // flag them done, so other candidates in this region skip the flood.
    // Harmless outside the widen scan (chainId/regionDone are rebuilt by
    // the next buildChainMap and never read during playouts).
    uint8_t code = (unsettled || !owner) ? 0 : owner;
    for(uint8_t j = 0; j < cnt; j++) {
        uint8_t c = region[j];
        chainId[c] = (chainId[c] & 0x3F) | (code << 6);
        regionDone[c >> 3] |= bitMask(c);
    }
    if(vit != 0xFF)
        chainId[vit] = (chainId[vit] & 0x3F) | (3 << 6);
    return ((uint16_t)vit << 8) | (unsettled ? 0 : owner);
}

// Pass-decision helper: the single stone colour bordering the empty region
// containing `seed`, or 0 if it touches both colours (contested) or no stone
// (open board). Unlike regionVital this has NO size cap -- a large territory
// is still settled, and a region of >=8 empty cells is trivially two eyes, so
// its bordering group is alive and non-vital fills there are pure own-fill
// losses. Runs once per move (post-search) so a full flood into the free
// floodScratch is fine.
static uint8_t settledRegionColor(uint8_t seed) {
    newMark();
    uint8_t cnt = 1, head = 0, owner = 0;
    floodSlot(0) = seed;
    simMark[seed] = markEpoch;
    while(head < cnt) {
        uint8_t p = floodSlot(head++), q;
        FOR_EACH_NEIGHBOR(q, p) {
            uint8_t s = simBoard[q];
            if(s == EMPTY) {
                if(simMark[q] != markEpoch) { simMark[q] = markEpoch; floodSlot(cnt++) = q; }
            } else if(!owner) {
                owner = s;
            } else if(owner != s) {
                return 0;                 // both colours border it -> contested
            }
        }
    }
    return owner;                         // 0 = no bordering stone (open board)
}

// All orthogonal neighbors own color (or edge)
static uint8_t isOwnEye(uint8_t pos, uint8_t color) {
    uint8_t q;
    FOR_EACH_NEIGHBOR(q, pos)
        if(simBoard[q] != color) return 0;
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
    simBoard[pos] = color;

    // ONE fused neighbour walk (was three): the capture check, the
    // fast-path classification (empties a/b/e, connectivity) and the
    // material for ko detection all come from this pass.
    // - The a/b/e/connected tallies are consumed only when captured==0,
    //   exactly when the board was unchanged during the walk, so they
    //   equal what a second walk would see. (When a capture DID happen
    //   mid-walk the tallies can be stale -- and are unused.)
    // - Ko (captured==1, single stone off): post-capture liberties of
    //   the placed lone stone = e + 1 (the captured cell), no other
    //   neighbour changed; lone = !connected. So the old third walk is
    //   `!connected && e == 0`.
    uint8_t captured = 0, capPos = 0;
    uint8_t a = 0xFF, b = 0xFF, e = 0, connected = 0;
    uint8_t q;
    FOR_EACH_NEIGHBOR(q, pos) {
        uint8_t s = simBoard[q];
        if(s == opp) {
            if(!hasLiberty(q)) {
                capPos = q;
                captured += removeGroup(q);
            }
        } else if(s == EMPTY) {
            if(a == 0xFF) a = q; else if(b == 0xFF) b = q;
            e++;
        } else
            connected = 1;
    }

    if(!captured) {
        if(noSelfAtari) {
            // Immediate-liberty fast-path (Pachi): a placement with no
            // same-colour neighbour is its own group, so its liberties
            // are exactly its empty neighbours -- in the same order
            // groupLibsFind reports them -- and no flood is needed. Only
            // a connected placement can borrow liberties and still floods.
            // One flood/scan covers both suicide (0 libs) and self-atari
            // (1); the result is cached for the next playout move, which
            // classifies this same group on an unchanged board.
            uint8_t libs;
            if(connected) {
                uint32_t gr = groupLibsFind(pos);
                libs = (uint8_t)gr;
                a = (uint8_t)(gr >> 8);
                b = (uint8_t)(gr >> 16);
            } else
                libs = e;
#ifdef PLAYOUT_STATS
            if(libs >= 2) { if(connected) spFloodOK++; else spLoneOK++; }
            else          { if(connected) spFloodRej++; else spLoneRej++; }
#endif
            if(libs < 2) {
#if PLAYOUT_THROWIN_RATE
                // throw-in exception (see PLAYOUT_THROWIN_RATE above)
                if(!(!connected && libs == 1 &&
                     (PLAYOUT_THROWIN_RATE == 1 ||
                      (rnd16() & (PLAYOUT_THROWIN_RATE - 1)) == 0)))
#endif
                {
                    // barrier: recompute &simBoard[pos] here (4 cycles,
                    // reject path) instead of holding it in a saved
                    // register pair across every call (8 cycles, always)
                    asm volatile("" : "+r"(pos));
                    simBoard[pos] = EMPTY;
                    return ILLEGAL;
                }
            }
            cacheLibsPos = pos;
            cacheLibs = libs;
            cacheL1 = a;
            cacheL2 = b;
        } else {
            if(!hasLiberty(pos)) { // suicide
                asm volatile("" : "+r"(pos));  // barrier: see above
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

    // Simple ko (see the fused-walk comment above)
    if(captured == 1 && !connected && e == 0) return capPos;
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
        uint32_t gr = groupLibsFind(esc);
        uint8_t libs = (uint8_t)gr, l1 = (uint8_t)(gr >> 8),
                l2 = (uint8_t)(gr >> 16);
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

            uint8_t room = 0;
            uint8_t q;
            FOR_EACH_NEIGHBOR(q, other)
                if(simBoard[q] == EMPTY) room++;
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
// Shared base-3 index over the on-board neighbors; sets *pcls, *pstones.
// Edge-class half of pattern3Index, kept out of line so the interior
// majority of patternBonus calls doesn't inherit this path's register
// frame (the x/y counters + 16-bit mult/idx pairs drove a 9-register
// prologue on every call). Packed return: idx | stones<<16 | cls<<24.
__attribute__((optimize("O2"), noinline))
static uint32_t pattern3Edge(int8_t cx, int8_t cy, uint8_t color) {
    uint8_t clsx = (cx == 0) ? 0 : (cx == BOARD_SIZE - 1) ? 2 : 1;
    uint8_t clsy = (cy == 0) ? 0 : (cy == BOARD_SIZE - 1) ? 2 : 1;
    uint16_t idx = 0;
    uint8_t stones = 0;
    uint16_t mult = 1;
    const uint8_t *b = simBoard + cy * BOARD_SIZE + cx;   // 3x3 centre
    for(int8_t dy = -1; dy <= 1; dy++) {
        int8_t r9 = dy * BOARD_SIZE;   // row offset, hoisted
        uint8_t rowOff = (uint8_t)(cy + dy) >= BOARD_SIZE; // hoisted y check
        for(int8_t dx = -1; dx <= 1; dx++) {
            if(dx == 0 && dy == 0) continue;
            int8_t x = cx + dx;
            if(rowOff || x < 0 || x >= BOARD_SIZE)
                continue; // off-board cells are implied by the class
            uint8_t s = b[dx + r9];
            uint8_t v = (s == EMPTY) ? 0 : (s == color) ? 1 : 2;
            if(v) stones++;
            idx += v * mult;
            mult *= 3;
        }
    }
    return (uint32_t)idx | ((uint32_t)stones << 16) |
           ((uint32_t)(clsy * 3 + clsx) << 24);
}

static uint16_t pattern3Index(int8_t cx, int8_t cy, uint8_t color,
                              uint8_t *pcls, uint8_t *pstones) {
    uint16_t idx = 0;
    uint8_t stones = 0;
    if((uint8_t)(cx - 1) < BOARD_SIZE - 2 && (uint8_t)(cy - 1) < BOARD_SIZE - 2) {
        // Left-right mirror fold: read the 8 neighbours as own(1)/opp(2)/
        // empty(0), then index the interior by (middle spine, unordered pair
        // of {left,right} columns) with a dense triangular index. Halves the
        // interior table; mirror-equivalent shapes share a slot.
        // Sequential pointer walk: row-adjacent cells load with 2-cycle
        // ld Z+ instead of per-read 16-bit address arithmetic.
        const uint8_t *p = simBoard + cy * BOARD_SIZE + cx - 10;
#define VC(s) ((s) ? (((s) == color) ? 1 : 2) : 0)
        uint8_t vNW = VC(*p); p++;
        uint8_t vN  = VC(*p); p++;
        uint8_t vNE = VC(*p); p += 7;
        uint8_t vW  = VC(*p); p += 2;
        uint8_t vE  = VC(*p); p += 7;
        uint8_t vSW = VC(*p); p++;
        uint8_t vS  = VC(*p); p++;
        uint8_t vSE = VC(*p);
#undef VC
        stones = (vNW>0)+(vN>0)+(vNE>0)+(vW>0)+(vE>0)+(vSW>0)+(vS>0)+(vSE>0);
        uint8_t L = vNW + 3*vW + 9*vSW;   // left column  0..26
        uint8_t R = vNE + 3*vE + 9*vSE;   // right column 0..26
        uint8_t M = vN + 3*vS;            // middle spine 0..8
        if(L > R) { uint8_t t = L; L = R; R = t; }
        // M*378 + L*27 - L*(L-1)/2 by table: the combine's three 16-bit
        // multiplies have tiny domains (M<=8, L<=26), so two PROGMEM
        // word loads replace them. Same values by construction.
        static const uint16_t PROGMEM FOLD_M[9] = {0, 378, 756, 1134, 1512, 1890, 2268, 2646, 3024};
        static const uint16_t PROGMEM FOLD_TRI[27] = {0, 27, 53, 78, 102, 125, 147, 168, 188, 207, 225, 242, 258, 273, 287, 300, 312, 323, 333, 342, 350, 357, 363, 368, 372, 375, 377};
        idx = pgm_read_word(FOLD_M + M) + pgm_read_word(FOLD_TRI + L) +
              (uint8_t)(R - L);
    } else {
        uint32_t r = pattern3Edge(cx, cy, color);
        *pcls = (uint8_t)(r >> 24);
        *pstones = (uint8_t)(r >> 16);
        return (uint16_t)r;
    }
    *pcls = 4;   // interior: clsy*3+clsx == 4 by construction
    *pstones = stones;
    return idx;
}


#include "pattern_weights.h"
// Signed data-learned 3x3 pattern weight (replaces the MoGo bit).
__attribute__((optimize("O2")))
static int8_t patternBonus(int8_t cx, int8_t cy, uint8_t color) {
    uint8_t cls, stones;
    uint16_t idx = pattern3Index(cx, cy, color, &cls, &stones);
    if(stones < 2) return 0;
    uint16_t n = pgm_read_word(PAT3W_BASE + cls) + idx;
    uint8_t bits = pgm_read_byte(PAT3W_BITS + (n >> 2));
    // >> ((n&3)*2) is a variable shift = a cycle-per-step loop on AVR;
    // two conditional constant shifts (swap+andi / 2x lsr) are fixed cost
    if(n & 2) bits >>= 4;
    if(n & 1) bits >>= 2;
    return (int8_t)pgm_read_byte(PAT3W_LEVEL + (bits & 3));
}

// Dynamic komi for graceful losing (see think): when behind, the
// tree learns from playouts scored with this many HALF-POINTS
// spotted to the root player, so "lose by less" reads as winning
// and the search plays normal margin-preserving moves instead of
// maximizing the tiny chance of an opponent collapse (the flail).
// True-komi results still drive resignation, passing, and the eval.

// Area scoring; returns winning color and leaves the raw doubled
// margin (black - white, komi not applied) for the virtual verdict
static int16_t lastMargin2;
static uint8_t scoreWinner() {
    uint8_t black = 0, white = 0;
    for(uint8_t i = 0; i < BOARD_CELLS; i++) {
        if(simBoard[i] == BLACK) { black++; continue; }
        if(simBoard[i] == WHITE) { white++; continue; }
        uint8_t tb = 0, tw = 0;
        uint8_t q;
        FOR_EACH_NEIGHBOR(q, i) {
            if(simBoard[q] == BLACK) tb = 1;
            if(simBoard[q] == WHITE) tw = 1;
        }
        if(tb && !tw) black++;
        else if(tw && !tb) white++;
    }
    lastMargin2 = (int16_t)black * 2 - (int16_t)white * 2;
    return lastMargin2 > (int16_t)simKomi ? BLACK : WHITE;
}

// The same playout under the virtual komi: the root player wins if
// within vKomi2 half-points of the real bar
static uint8_t vKomiWinner() {
    int16_t bar = (int16_t)simKomi;
    if(rootTurn == BLACK) bar -= vKomi2;
    else bar += vKomi2;
    return lastMargin2 > bar ? BLACK : WHITE;
}

// Playout capture tallies (statics so the shared move helper can
// update them; reset at each playout start)
static uint8_t capB, capW;

// Attempt one playout move at pos (0xFF = none): the legality gate,
// gated play, and bookkeeping shared by every playout heuristic.
// Returns 1 when the move was played; caller flips toMove.
// Scoring playouts (see scoreDead) allow self-atari: killing an
// eyespaced-but-dead group needs throw-in sacrifices, and with the
// gate on, playouts defended square four successfully (3/64 kills)
// while any correct line captures the sacrifices right back — so
// ownership tallies stay honest. (scoreMode is declared up top.)
__attribute__((noinline))
__attribute__((optimize("O2")))
// Returns 0 on failure, else (0x100 | newKo): ko travels by VALUE and
// comes back in the result, `last` is the caller's own pos -- the two
// pointer out-params this replaces cost two derefs on entry and two
// indirect stores on success, on a ~100K-calls-per-think path.
static uint16_t playoutTry(uint8_t pos, uint8_t toMove, uint8_t ko,
                           uint8_t m) {
    if(pos >= BOARD_CELLS || pos == ko || simBoard[pos] != EMPTY ||
       isOwnEye(pos, toMove)) return 0;
    uint8_t nk = simPlay(pos, toMove, ko, !scoreMode);
    if(nk == ILLEGAL) return 0;
    // barrier: raveMark below re-derives its bitmap address from this
    // one register instead of GCC also keeping a zero-extended copy of
    // pos in a second call-saved pair across the simPlay call
    asm volatile("" : "+r"(pos));
    if(toMove == rootTurn && m < RAVE_HORIZON) raveMark(pos);
    if(simCaptured) {
        if(toMove == BLACK) capB += simCaptured;
        else capW += simCaptured;
    }
    return 0x100 | nk;
}

#ifdef PLAYOUT_STATS
uint32_t plN, plMoves, plEndCap, plEndPass, plEndMercy;
#endif
#ifdef DECIDE_PROBE
// probe-only: iteration at which the visit-argmax last changed
uint16_t dpLastChange; uint8_t dpPrevBest;
#endif
#ifdef WIDEN_PROBE
// probe-only: widening-waste statistics
uint16_t wpCalls, wpAdded, wpEmpty, wpAllocFail;
#endif
#ifdef PLAYOUT_SNAP
// probe-only: board snapshots at fixed playout depths
uint8_t plSnap40[81], plSnap60[81], plSnap80[81];
#endif
#ifdef PLAYOUT_SNAP_EMPT
// probe-only: snapshots when the empty count first reaches thresholds
uint8_t plSnapThresh[4] = {12, 10, 8, 6};
uint8_t plSnapE[4][81];
uint8_t plSnapMove[4]; // 0xFF = not yet reached
#endif
// Random playout from current simBoard; returns winning color.
// `last` = the previous move (0xFF/pass = none).
__attribute__((optimize("O2")))
static uint8_t playout(uint8_t toMove, uint8_t ko, uint8_t last) {
    uint8_t passes = 0;
    capB = capW = 0;
#ifdef PLAYOUT_STATS
    uint8_t psM = 0, psMercy = 0;
#define PS_TICK psM = m + 1;
#else
#define PS_TICK
#endif
    for(uint8_t m = 0; m < PLAYOUT_CAP && passes < 2; m++) {
        PS_TICK
#ifdef PLAYOUT_SNAP
        if(m == 40) memcpy(plSnap40, simBoard, 81);
        else if(m == 60) memcpy(plSnap60, simBoard, 81);
        else if(m == 80) memcpy(plSnap80, simBoard, 81);
#endif
#ifdef PLAYOUT_SNAP_EMPT
        {
            uint8_t emp = 0;
            for(uint8_t i = 0; i < BOARD_CELLS; i++)
                if(simBoard[i] == EMPTY) emp++;
            for(uint8_t t = 0; t < 4; t++)
                if(emp <= plSnapThresh[t] && plSnapMove[t] == 0xFF) {
                    memcpy(plSnapE[t], simBoard, 81);
                    plSnapMove[t] = m;
                }
        }
#endif
        // Mercy rule: a lopsided capture balance has decided the game;
        // the area score already reflects it, skip the remaining fill
        if(capB > capW + MERCY_MARGIN || capW > capB + MERCY_MARGIN) {
#ifdef PLAYOUT_STATS
            psMercy = 1;
#endif
            break;
        }

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
                uint32_t gr = groupLibsFind(last);
                libs = (uint8_t)gr;
                l1 = (uint8_t)(gr >> 8);
                l2 = (uint8_t)(gr >> 16);
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
                uint8_t q;
                FOR_EACH_NEIGHBOR(q, last) {
                    if(simBoard[q] != toMove) continue;
                    uint8_t sl = soleLiberty(q);
                    // Short read: playouts rarely need long ladders,
                    // and a truncated read defaults to "escape"
                    if(sl != 0xFF && sl != ko &&
                       ladderEscapes(q, sl, 8)) {
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
        // Gated 1/4 (michi-style probabilistic gating; was 3/4). This
        // block is ~2.7% of think and pure overhead -- unlike the
        // capture/save heuristics it does NOT shorten playouts, so
        // firing it less is a near-free speed win. 1/4 measured
        // strength-neutral vs 3/4 (136 vs 125 / 1000 @ L0, z=+0.73);
        // 25% firing still catches dead shapes across a playout.
        if(last < BOARD_CELLS && !(rnd16() & 3)) {
            uint8_t vcand = 0xFF;
            uint8_t q;
            FOR_EACH_NEIGHBOR(q, last) {
                if(simBoard[q] != EMPTY) continue;
                uint16_t rv = regionVital(q);
                if((uint8_t)rv) vcand = rv >> 8;
                break; // only the first adjacent region
            }
            uint16_t r = playoutTry(vcand, toMove, ko, m);
            if(r) {
                ko = (uint8_t)r;
                last = vcand;
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
            uint16_t r = playoutTry(vp, toMove, ko, m);
            if(r) {
                ko = (uint8_t)r;
                last = vp;
                passes = 0;
                toMove = 3 - toMove;
                continue;
            }
        }

        // 3x3 patterns at the 8 points around the last move. Rate 3/4
        // (michi-style probabilistic gating): measured strength-neutral
        // vs firing 15/16 (125 vs 127 / 1000 games @ L0, z=-0.13) while
        // skipping the ~8-point pattern scan more often. 1/2 overshot
        // (-2.1pp), so 3/4 is the elbow.
        if(last < BOARD_CELLS && (rnd16() & 3)) {
            uint8_t lpxy = posXY(last);
            int8_t lpx = lpxy & 0x0F, lpy = lpxy >> 4;
            uint8_t matches[8];
            uint8_t nMatches = 0;
            for(int8_t dy = -1; dy <= 1; dy++) {
                for(int8_t dx = -1; dx <= 1; dx++) {
                    // no (0,0) skip: the centre is `last` itself, always
                    // occupied by the stone just played, so the EMPTY
                    // check below rejects it -- same matches. (A D8
                    // PROGMEM-table flatten of this loop measured +0.16%
                    // SLOWER: 3 lpm/point beats the loop machinery here,
                    // unlike keima where most iterations were filtered.)
                    int8_t cx = lpx + dx, cy = lpy + dy;
                    if(cx < 0 || cx >= BOARD_SIZE || cy < 0 || cy >= BOARD_SIZE)
                        continue;
                    uint8_t pos = cy * BOARD_SIZE + cx;
                    if(simBoard[pos] != EMPTY || pos == ko) continue;
                    if(isOwnEye(pos, toMove)) continue;
                    if(
                       patternBonus(cx, cy, toMove) > 0
                      )
                        matches[nMatches++] = pos;
                }
            }
            if(nMatches) {
                uint8_t mp = matches[rnd(nMatches)];
                uint16_t r = playoutTry(mp, toMove, ko, m);
                if(r) {
                    ko = (uint8_t)r;
                    last = mp;
                    passes = 0;
                    toMove = 3 - toMove;
                    continue;
                }
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
            uint8_t lxy = posXY(last);
            int8_t lx = lxy & 0x0F, ly = lxy >> 4;
            if(p & 1) {
                int8_t cx = lx + (int8_t)rndMod<3>() - 1;
                int8_t cy = ly + (int8_t)rndMod<3>() - 1;
                if(cx >= 0 && cx < BOARD_SIZE && cy >= 0 && cy < BOARD_SIZE)
                    pos = cy * BOARD_SIZE + cx;
            } else if(rootStones + m >= EARLY_STONES &&
                      (p & ((CONTACT_ANSWER_MASK | LONE_ANSWER_MASK) << 1))
                          != 0) {
                // union pre-gate: if neither case's coin could pass,
                // skip the classification scan (with equal masks this
                // is exactly the pre-split gate — no extra work)
                // (gated: in the opening EVERY stone is "lone" and
                // every attachment is normal — these answer rules
                // only mean something once territory has shape)
                // Answer a stone that CONTACTS us (a boundary push)
                // or one with no support within 2 (an invasion);
                // either way the reply is a contact move. The two
                // cases carry SEPARATE probability masks.
                uint8_t lastColor = simBoard[last];
                // One pass over last's neighbours: collect them into nbl[]
                // (needed for the random pick nbl[rnd(nl)] below), count nl,
                // and flag a contact push -- no break, nl must count all.
                uint8_t nbl[4], nl = 0, contact = 0, answer = 0, q;
                FOR_EACH_NEIGHBOR(q, last) {
                    nbl[nl++] = q;
                    if(simBoard[q] == toMove) contact = 1; // contact push
                }
                if(contact) {
                    answer = (p & (CONTACT_ANSWER_MASK << 1)) != 0;
                } else if((p & (LONE_ANSWER_MASK << 1)) != 0) {
                    // 5x5 support scan only after the coin passes
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
            if(pos != 0xFF) {
                uint16_t r = playoutTry(pos, toMove, ko, m);
                if(r) {
                    ko = (uint8_t)r;
                    last = pos;
                    passes = 0;
                    toMove = 3 - toMove;
                    continue;
                }
            }
        }

        uint8_t start = rndMod<BOARD_CELLS>();
        uint8_t played = 0;
        // Two-phase circular scan (start..80, then 0..start-1): identical
        // visit order to the old start+i-with-wrap, but the per-cell wrap
        // check -- the hottest inlined line in the engine -- is gone; the
        // phase switch runs once. continue still steps pos++ naturally.
        uint8_t pos = start, scanEnd = BOARD_CELLS;
        for(;; pos++) {
            if(pos >= scanEnd) {
                if(scanEnd != BOARD_CELLS || start == 0) break;
                pos = 0; scanEnd = start;   // phase 2: 0..start-1
            }
            if(simBoard[pos] != EMPTY || pos == ko) continue;

            // Lonely first-line moves are pure noise: skip unless the
            // point touches a stone. Slot 3 == 0xFF <=> <4 neighbours
            // <=> first line.
            const uint8_t *ne = NEIGHBOR_TABLE + pos * 5;
            uint8_t fourth = pgm_read_byte(ne + 3);  // 0xFF iff <4 neighbours
            if(fourth == 0xFF) {
                uint8_t contact = 0, q;
                while((q = pgm_read_byte(ne++)) != 0xFF) {
                    if(simBoard[q] != EMPTY) { contact = 1; break; }
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
            if(fourth != 0xFF && rootStones + m >= EARLY_STONES &&
               (rnd16() & PLAYOUT_GROW_MASK)) {
                uint8_t bxy = posXY(pos);
                uint8_t bx = bxy & 0x0F, by = bxy >> 4;
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

            uint16_t r = playoutTry(pos, toMove, ko, m);
            if(!r) continue;
            ko = (uint8_t)r;
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
#ifdef PLAYOUT_STATS
    plN++; plMoves += psM;
    if(psMercy) plEndMercy++;
    else if(passes >= 2) plEndPass++;
    else plEndCap++;
#endif
#undef PS_TICK
    return scoreWinner();
}

// ==================== Dead-stone scoring ====================
// The static scorer counts any one-color-bordered region as
// territory, so a dead group whose eyespace can never be two eyes
// (a square four in a real game) scores as alive — flipping the
// game result. At the end, vote with light playouts: a stone whose
// cell finishes opponent-owned in most of them is dead; remove it
// as a capture, then score the cleaned board statically. The
// scoring screen also gets the cleaned board, so dead stones
// visibly come off at the count.
#ifndef SCORE_PLAYOUTS
#define SCORE_PLAYOUTS 64
#endif
// Fill simBoard from the game and collect the eyespace vital points.
// Shared by think() and scoreDead().
static void loadRootBoard(Game &game) {
    for(uint8_t i = 0; i < BOARD_CELLS; i++)
        simBoard[i] = packedGet(game.board, i);
    nRootVitals = 0;
    for(uint8_t i = 0; i < BOARD_CELLS && nRootVitals < 3; i++) {
        if(simBoard[i] != EMPTY) continue;
        uint16_t rv = regionVital(i);
        if((uint8_t)rv && (rv >> 8) == i)
            rootVitals[nRootVitals++] = i;
    }
}

// The ownership vote itself, shared by scoreDead and the settle gate
// below: SCORE_PLAYOUTS light scoring playouts from the real board,
// counting per cell how often it finishes black-owned (black stone, or
// empty bordered only by black — same rules as scoreWinner).
static void ownVote(Game &game, uint8_t *own) {
    for(uint8_t i = 0; i < BOARD_CELLS; i++) own[i] = 0;

    rootTurn = game.turn;
    simKomi = game.kpieces;
    rootLast = 0xFF;
    rootKo = NO_KO;
    scoreMode = 1; // before loadRootBoard: square-four vitals apply
    loadRootBoard(game);
    rootStones = 0;
    for(uint8_t i = 0; i < BOARD_CELLS; i++)
        if(simBoard[i] != EMPTY) rootStones++;

    for(uint8_t p = 0; p < SCORE_PLAYOUTS; p++) {
        for(uint8_t i = 0; i < BOARD_CELLS; i++)
            simBoard[i] = packedGet(game.board, i);
        playout(game.turn, NO_KO, 0xFF);
        // Per-cell black ownership, same rules as scoreWinner
        for(uint8_t i = 0; i < BOARD_CELLS; i++) {
            uint8_t s = simBoard[i];
            if(s == WHITE) continue;
            if(s == BLACK) { own[i]++; continue; }
            uint8_t tb = 0, tw = 0;
            uint8_t q;
            FOR_EACH_NEIGHBOR(q, i) {
                if(simBoard[q] == BLACK) tb = 1;
                if(simBoard[q] == WHITE) tw = 1;
            }
            if(tb && !tw) own[i]++;
        }
    }
    scoreMode = 0;
}

// Ownership-corrected settle margin for the opponent-just-passed
// decision: the honest area margin (doubled, komi applied, positive =
// side to move wins) of the board as the scoreDead vote reads it. This
// is the SAME vote game-over scoring applies, so a pass taken on this
// verdict scores the way the verdict says. SETTLE_NONE before the
// endgame — with open space the vote is coin flips, not a count.
#define SETTLE_NONE (-32768)
static uint8_t countStones(Game &game) {
    uint8_t st = 0;
    for(uint8_t i = 0; i < BOARD_CELLS; i++)
        if(packedGet(game.board, i) != EMPTY) st++;
    return st;
}
static int16_t settleVote(Game &game) {
    if(countStones(game) < 45) return SETTLE_NONE;
    // The node pool is rebuilt from scratch every think, so the screen
    // buffer is free scratch here just as it is in scoreDead.
    uint8_t *own = Arduboy2Base::sBuffer;
    ownVote(game, own);
    uint8_t b = 0;
    for(uint8_t i = 0; i < BOARD_CELLS; i++)
        if(own[i] >= SCORE_PLAYOUTS / 2) b++;
    int16_t m2 = (int16_t)b * 4 - BOARD_CELLS * 2; // 2*(black - white) area
    return (game.turn == BLACK) ? m2 - (int16_t)simKomi
                                : (int16_t)simKomi - m2;
}

void AI::scoreDead(Game &game) {
    uint8_t *own = Arduboy2Base::sBuffer; // free once the game is over
    ownVote(game, own);

    for(uint8_t i = 0; i < BOARD_CELLS; i++) {
        uint8_t s = packedGet(game.board, i);
        uint8_t blackOwned = own[i] >= SCORE_PLAYOUTS / 2;
        if(s == BLACK && !blackOwned) {
            game.set(i % BOARD_SIZE, i / BOARD_SIZE, EMPTY);
            game.captures[1]++;
        } else if(s == WHITE && blackOwned) {
            game.set(i % BOARD_SIZE, i / BOARD_SIZE, EMPTY);
            game.captures[0]++;
        }
    }
    game.computeScore();
}

#ifdef LATENT_DEBUG
uint8_t dbgWatch = 0xFF;
#endif
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
#ifdef LATENT_DEBUG
    if(path[0] != 0 || pathDepth > 31) {
        fprintf(stderr, "PATH CORRUPT at reclaim: path[0]=%u pathDepth=%u poolUsed=%u\n",
                path[0], pathDepth, poolUsed);
        abort();
    }
#endif
    // Latents first: a pending pre-scanned candidate (move bit7) is
    // pure cache -- one node, no subtree, no information loss; a
    // future scan simply recreates it. Freeing one costs nothing,
    // unlike evicting a real subtree. Latents are never on the path
    // (path nodes were selected, which latents cannot be).
    for(uint8_t d = 0; d < pathDepth; d++) {
        uint8_t prev = 0xFF;
        for(uint8_t c = node(path[d]).firstChild; c != 0xFF;
            prev = c, c = node(c).nextSibling) {
            if(!(node(c).move & 0x80)) continue;
#ifdef LATENT_DEBUG
            fprintf(stderr, "RECLAIM latent c=%u move=%u parent=%u d=%u pathDepth=%u\n",
                    c, node(c).move & 0x7F, path[d], d, pathDepth);
#endif
            if(prev == 0xFF)
                node(path[d]).firstChild = node(c).nextSibling;
            else
                node(prev).nextSibling = node(c).nextSibling;
            node(c).nextSibling = freeHead;
            freeHead = c;
            return 1;
        }
    }
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
__attribute__((optimize("O2")))
static uint8_t buildNearMask(uint8_t *near) {
    memset(near, 0, 12);
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
        // The row span [x0,x1] is a contiguous bit-run in the linear
        // layout (rows are BOARD_SIZE wide and the run never crosses a
        // row edge), so set the whole run per row instead of one bit
        // per cell. `run` (the unshifted mask) is constant across the
        // <=5 rows; b walks the rows by +BOARD_SIZE. The run spans at
        // most 2 bytes, so near[] is sized 12 (byte 11 is write-only).
        uint8_t run = (uint8_t)((1u << (x1 - x0 + 1)) - 1);
        uint8_t b = y0 * BOARD_SIZE + x0;
        for(uint8_t yy = y0; yy <= y1; yy++, b += BOARD_SIZE) {
            uint16_t m = (uint16_t)run << (b & 7);
            near[b >> 3]       |= (uint8_t)m;
            near[(b >> 3) + 1] |= (uint8_t)(m >> 8);
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
    newMark();
    uint8_t eLibs = groupLibsMark(eStart);
    if(eLibs > 3) return 0;
    uint8_t fMin = 0xFF;
    for(uint8_t p = 0; p < BOARD_CELLS; p++) {
        if(simBoard[p] != eColor || simMark[p] != markEpoch) continue;
        uint8_t q;
        FOR_EACH_NEIGHBOR(q, p) {
            if(simBoard[q] == 3 - eColor && simMark[q] != markEpoch) {
                uint8_t fl = groupLibsMark(q); // joins the same epoch
                if(fl < fMin) fMin = fl;
            }
        }
    }
    return fMin <= 3 && eLibs <= fMin;
}
#endif

// The 8 knight (keima) offsets, in the exact order the old
// a=-2..2,b=-2..2 / a*a+b*b==5 filter produced them, so the keima loop's
// first-match break stays identical (iterate 8, not 25 with 17 discarded).
// entries 0-7: knight offsets; entries 8-11: straight one-point jumps
// Companion tables: KEIMA_L[ki] = a + 9b (partner as a linear offset;
// L/2 is also the jump midpoint for ki>=8, and L-a recovers the b-row
// corner), KEIMA_W1/W2[ki] = the two waist offsets as constants --
// replaces the b*9 multiply and the waist-selection branch per entry.
static const int8_t PROGMEM KEIMA_L[12] = {-11, 7, -19, 17, -17, 19, -7, 11, -18, 18, -2, 2};
static const int8_t PROGMEM KEIMA_W1[8] = {-1, -1, -9, 9, -9, 9, 1, 1};
static const int8_t PROGMEM KEIMA_W2[8] = {-10, 8, -10, 8, -8, 10, -8, 10};

static const int8_t PROGMEM KEIMA_A[12] = {-2, -2, -1, -1,  1,  1,  2,  2,
                                            0,  0, -2,  2};
static const int8_t PROGMEM KEIMA_B[12] = {-1,  1, -2,  2, -2,  2, -1,  1,
                                           -2,  2,  0,  0};
// Per-coordinate keima bounds masks: bit ki of KEIMA_MX[x] is set iff
// x + KEIMA_A[ki] is on-board (KEIMA_MY likewise for y + KEIMA_B[ki]).
// Their AND decides all 12 bounds tests at once; KEIMA_B itself is now
// used only by the table generator above.
static const uint16_t PROGMEM KEIMA_MX[9] = {0xBF0, 0xBFC, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0x73F, 0x70F};
static const uint16_t PROGMEM KEIMA_MY[9] = {0xEAA, 0xEEB, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xDD7, 0xD55};

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

    // Group-aware neighbor scan over the precomputed chain map
    // (buildChainMap runs once per expansion): distinct chains per
    // color and their weakest liberty classes — pure array reads,
    // no floods (the per-candidate chain floods here were the
    // biggest single cost in the whole search).
    uint8_t fGroups = 0, eGroups = 0, emptyN = 0;
    uint8_t fMinLibs = 0xFF, eMinLibs = 0xFF;
    uint8_t doomCand[4];
    uint8_t nDoom = 0;
    uint8_t fIds[4];  // ids of the distinct friendly chains seen
    uint8_t fSeed[4]; // a stone of each (soleConnector floods from it)
    uint8_t seen[4];
    uint8_t nSeen = 0;
#if PRIOR_RACE
    uint8_t raceCand = 0xFF; // enemy chain at 2-3 libs, race check later
#endif
    uint8_t q;
    FOR_EACH_NEIGHBOR(q, pos) {
        uint8_t id = CHAIN_OF(chainId[q]);
        if(!id) { emptyN++; continue; }   // empty neighbour = a liberty
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
            if(fGroups < 4) { fIds[fGroups] = id; fSeed[fGroups] = q; }
            fGroups++;
            if(l < fMinLibs) fMinLibs = l;
            if(l == 1) doomCand[nDoom++] = q;
            else if(l == 2) sawWeakFriend = 1;
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
                  soleConnector(fSeed[0], fIds[1])) {
            bonus += PRIOR_CONNECT;
            connHere = 1;
        }
    }

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
        // Eyespace lookup: pos is empty, so its chainId top bits hold the
        // region code — 0=open, 1=black, 2=white, 3=vital. Filled lazily
        // by regionVital (first candidate in a region floods it; the rest
        // read the cache), so this is O(1) per candidate.
        uint8_t rb = pos >> 3;  // 8-bit shift (see widenNode scan)
        if(!(regionDone[rb] & bitMask(pos)))
            regionVital(pos); // cache side-effect only
        uint8_t rc = chainId[pos] >> 6;
        if(rc == 3) {
            bonus += PRIOR_VITAL;
            vitalHere = 1;
        } else if(rc && !sawCapture && !sawSave && !sawAtari &&
                  !connHere) {
            bonus -= (rc == toMove) ? PRIOR_OWNFILL_PENALTY
                                    : PRIOR_INVADE_PENALTY;
        }
    }

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
        // Immediate-liberty fast-path (Pachi): the merged group's
        // liberty count is known without a flood in two common cases.
        // A lone stone (no friendly neighbour) has exactly its own empty
        // neighbours as liberties; any point with >=3 empty neighbours
        // has >=3 liberties regardless of what it connects to (and the
        // flood, capped at 3, would report exactly 3). Both give the
        // same penalty decision as the flood, so they skip it outright.
        // Only a friendly-connected point with <=2 empties can gain
        // extra liberties through the connection and still needs it.
        uint8_t libs;
        if(fGroups == 0)     libs = emptyN;   // lone stone: libs == empties
        else if(emptyN >= 3) libs = 3;        // >=3 empties => >=3 libs
        else {
            simBoard[pos] = toMove;
            libs = (uint8_t)groupLibsFind(pos);
            simBoard[pos] = EMPTY;
        }
        if(libs <= 2)
            bonus -= (libs <= 1) ? PRIOR_SELFATARI_PENALTY
                                 : PRIOR_THIN_PENALTY;
    }

    // Center preference: the edge is worth less than the third line
    uint8_t xy = posXY(pos);
    uint8_t x = xy & 0x0F, y = xy >> 4;
    uint8_t ex = x < BOARD_SIZE - 1 - x ? x : BOARD_SIZE - 1 - x;
    uint8_t ey = y < BOARD_SIZE - 1 - y ? y : BOARD_SIZE - 1 - y;
    uint8_t ed = ex < ey ? ex : ey;
    bonus += ed > PRIOR_CENTER_MAX ? PRIOR_CENTER_MAX : ed;

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
    if(!isFar && !sawCapture && !sawSave && !sawAtari && !hasOrthFriend) {
        // (isFar: no stone within Chebyshev 2, and every cell this
        // block reads -- diagonals, keima partners, jump midpoints --
        // lies within that radius, so all 16 probes are guaranteed
        // misses; skipping is byte-identical)
        // All board reads here are cells of a fixed shape around the
        // candidate, so they're pos + a linear offset -- no Y*9 multiply.
        // Bounds are still checked on the (x,y) coords before each read.
        uint8_t hasDiagFriend = 0;
        for(int8_t dy = -1; dy <= 1; dy += 2)
            for(int8_t dx = -1; dx <= 1; dx += 2) {
                int8_t fx = x + dx, fy = y + dy;
                if(fx < 0 || fx >= BOARD_SIZE || fy < 0 || fy >= BOARD_SIZE)
                    continue;
                if(simBoard[pos + dx + dy * BOARD_SIZE] == toMove)
                    hasDiagFriend = 1;
            }
        if(!hasDiagFriend) {
            uint8_t penalized = 0;
            uint16_t kmask = pgm_read_word(KEIMA_MX + x) &
                             pgm_read_word(KEIMA_MY + y);
            for(uint8_t ki = 0; ki < 12 && !penalized; ki++, kmask >>= 1) {
                if(!(kmask & 1)) continue;  // partner off-board
                int8_t a = (int8_t)pgm_read_byte(KEIMA_A + ki);
                {
                    int8_t L = (int8_t)pgm_read_byte(KEIMA_L + ki);
                    if(simBoard[pos + L] != toMove) continue;
                    if(ki >= 8) {
                        // one-point jump: single midpoint, must be empty,
                        // enemy on a SIDE of it (the push-in cut). Sides
                        // are perpendicular to the jump line.
                        int8_t m = L / 2;
                        if(simBoard[pos + m] != EMPTY) continue;
                        uint8_t hit = 0;
                        if(a) { // horizontal jump: sides above/below midpoint
                            int8_t my = y;
                            if(my > 0 && simBoard[pos + m - BOARD_SIZE] == opp) hit = 1;
                            if(my < BOARD_SIZE - 1 &&
                               simBoard[pos + m + BOARD_SIZE] == opp) hit = 1;
                        } else { // vertical jump: sides left/right of midpoint
                            if(x > 0 && simBoard[pos + m - 1] == opp) hit = 1;
                            if(x < BOARD_SIZE - 1 &&
                               simBoard[pos + m + 1] == opp) hit = 1;
                        }
                        if(hit) {
                            bonus -= PRIOR_JUMP_PENALTY;
                            penalized = 1;
                        }
                        continue;
                    }
                    // Back corners of the keima box, (kx,y) and (x,ky), must
                    // not be mine — else the pair links down the outside line
                    // and it isn't a lone keima.
                    if(simBoard[pos + a] == toMove ||
                       simBoard[pos + (int8_t)(L - a)] == toMove) continue;
                    // The two waist points between candidate and partner,
                    // constants per entry from the companion tables.
                    int8_t w1 = (int8_t)pgm_read_byte(KEIMA_W1 + ki);
                    int8_t w2 = (int8_t)pgm_read_byte(KEIMA_W2 + ki);
                    // Enemy exactly ON a waist = a supported cut. (The old
                    // oppNear fired on any enemy in the waist's 3x3 — ~5x more
                    // firings, mostly false; strength-equal at 1000 games L0.)
                    if(simBoard[pos + w1] == opp ||
                       simBoard[pos + w2] == opp) {
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
        if(!isFar && ed == 1 && rootStones >= EARLY_STONES) {
            // (isFar: the 5x5 is provably empty; nearEnemy stays 0
            // either way -- byte-identical skip)
            const uint8_t *rp = simBoard + pos - 2 * BOARD_SIZE;
            for(int8_t dy = -2; dy <= 2 && !nearEnemy;
                dy++, rp += BOARD_SIZE) {
                if((uint8_t)(y + dy) >= BOARD_SIZE) continue; // row off-board
                for(int8_t dx = -2; dx <= 2; dx++) {
                    if((uint8_t)(x + dx) >= BOARD_SIZE) continue;
                    if(rp[dx] == opp) {
                        nearEnemy = 1;
                        break;
                    }
                }
            }
        }
        if(!nearEnemy) {
            lowLineBad = 1;
            bonus -= (ed == 0) ? PRIOR_EDGE_PENALTY : PRIOR_LINE2_PENALTY;
        }
    }

    // Locality: adjacent or diagonal to the previous move.
    if(!lowLineBad && last < BOARD_CELLS) {
        int8_t dx = x - last % BOARD_SIZE; if(dx < 0) dx = -dx;
        int8_t dy = y - last / BOARD_SIZE; if(dy < 0) dy = -dy;
        if(dx <= 1 && dy <= 1) {
            bonus += PRIOR_LOCAL;
            // Contact-push block (see PRIOR_BLOCK): their stone
            // touches us, this candidate touches the pusher
            // ORTHOGONALLY (the junction does; diagonal near-misses
            // scored alike in a real cut-through game until this)
        }
    }

    // Local shape: the same 3x3 patterns the playouts use. This is what
    // makes cut-defense (blocking a keima push, connecting a jump)
    // visible to the tree instead of only to the rollouts.
    if(!lowLineBad && !isFar) bonus += patternBonus(x, y, toMove);
    // (isFar: the 3x3 is provably empty -> stones<2 -> patternBonus
    // returns 0, but only after computing the full pattern index;
    // skipping is byte-identical)

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

// Best pending latent of a node by RECOVERED prior: addChild seeded
// (V+p, W+p) for p>=0 and (V-p, W) for p<0, so p = (w-W == v-V) ?
// v-V : -(v-V). Order-independent, so partial batches and free-list
// reuse cannot skew activation order.
static uint8_t latentBest(uint8_t nodeIdx) {
    uint8_t best = 0xFF;
    int8_t bestP = -128;
    for(uint8_t c = node(nodeIdx).firstChild; c != 0xFF;
        c = node(c).nextSibling) {
        if(!(node(c).move & 0x80)) continue;
        int8_t dv = (int8_t)(nVisits(c) - PRIOR_BASE_V);
        int8_t p = ((int8_t)(nWins(c) - PRIOR_BASE_W) == dv) ? dv : -dv;
        if(p > bestP) { bestP = p; best = c; }
    }
    return best;
}

static uint8_t childCount(uint8_t nodeIdx) {
    // ACTIVE children only: latents (move bit7) hold a scanned-ahead
    // candidate but do not occupy a widening slot until activated
    uint8_t n = 0;
    for(uint8_t c = node(nodeIdx).firstChild; c != 0xFF; c = node(c).nextSibling)
        if(!(node(c).move & 0x80)) n++;
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
    for(uint8_t k = 0; k < ROOT_INIT; k++) {
        // burst-fill: drain stored latents before scanning again, so
        // the active set is the true prior top-N exactly as the
        // one-at-a-time schedule would build it
        uint8_t lat = latentBest(nodeIdx);
        if(lat != 0xFF) {
            node(lat).move &= 0x7F;
            continue;
        }
        if(!widenNode(nodeIdx, toMove, ko, last)) break;
    }
}

// Progressive widening: add the single best not-yet-present candidate.
// Poisoned children stay in the have-bitmap, so an illegal move is
// never re-added. Returns 1 if a child was added.
static uint8_t widenNode(uint8_t nodeIdx, uint8_t toMove, uint8_t ko, uint8_t last) {
    uint8_t have[11];
    memset(have, 0, sizeof(have));
    for(uint8_t c = node(nodeIdx).firstChild; c != 0xFF;) {
        Node &n = node(c);
        uint8_t m = n.move & 0x7F;
        if(m < BOARD_CELLS) {
            uint8_t mb = m >> 3;  // 8-bit shift: a promoted index shifts
                                  // in a 16-bit asr/ror loop at -Os
            have[mb] |= bitMask(m);
        }
        c = n.nextSibling;
    }

    uint8_t near[12];  // 12 not 11: buildNearMask's run write touches byte 11
    uint8_t anyStone = buildNearMask(near);
    buildChainMap();

    // Batch widening: one scan yields the top WIDEN_BATCH candidates
    // (the scan already ranks everything to find #1; tracking three is
    // ~free). Callers gate on childCount < maxKids, so a node that
    // received a batch simply skips its next widen triggers until the
    // visit schedule catches up -- same candidate sets, ~3x fewer
    // 81-cell scans (widenNode was 17.7% of think).
    int8_t bP[WIDEN_BATCH];
    uint8_t bPos[WIDEN_BATCH];
    for(uint8_t k = 0; k < WIDEN_BATCH; k++) { bP[k] = -128; bPos[k] = 0xFF; }
    uint8_t startPos = rndMod<BOARD_CELLS>();
    // Same two-phase circular scan as playout's global probe (see there).
    uint8_t pos = startPos, scanEnd = BOARD_CELLS;
    for(;; pos++) {
        if(pos >= scanEnd) {
            if(scanEnd != BOARD_CELLS || startPos == 0) break;
            pos = 0; scanEnd = startPos;    // phase 2: 0..startPos-1
        }
        if(simBoard[pos] != EMPTY || pos == ko) continue;
        uint8_t pb = pos >> 3;      // 8-bit shift, shared with near[]
        uint8_t pm = bitMask(pos);
        if(have[pb] & pm) continue;
        uint8_t isFar = 0;
        if(anyStone && !(near[pb] & pm)) {
            uint8_t bxy = posXY(pos);
            uint8_t bx = bxy & 0x0F, by = bxy >> 4;
            uint8_t bex = bx < BOARD_SIZE - 1 - bx ? bx : BOARD_SIZE - 1 - bx;
            uint8_t bey = by < BOARD_SIZE - 1 - by ? by : BOARD_SIZE - 1 - by;
            if((bex < bey ? bex : bey) < 2) continue;
            isFar = 1;
        }
        if(isOwnEye(pos, toMove)) continue;
        int8_t p = candidatePrior(pos, toMove, last, isFar);
        if(p > bP[WIDEN_BATCH - 1]) {
            uint8_t k = WIDEN_BATCH - 1;
            while(k > 0 && p > bP[k - 1]) {
                bP[k] = bP[k - 1];
                bPos[k] = bPos[k - 1];
                k--;
            }
            bP[k] = p;
            bPos[k] = pos;
        }
    }
    if(bPos[0] == 0xFF) {
#ifdef WIDEN_PROBE
        wpCalls++; wpEmpty++;
#endif
        return 0;
    }
#ifdef WIDEN_PROBE
    wpCalls++;
#endif
    // LATENT batch: #1 activates now; #2/#3 join the child list with
    // bit7 of move set (latent: invisible to selection until the widen
    // schedule reaches their slot and flips the flag -- see the
    // trigger in mctsIterate). The ACTIVE child allocates FIRST: under
    // pool pressure a partial batch must yield the active child, never
    // a latent-only node (selectChild's fallback once returned such a
    // latent and simPlay indexed simBoard[move|0x80] out of bounds --
    // the memory-corruption bug this ordering fixes). Activation picks
    // the best latent by RECOVERED PRIOR, so list order is free.
    uint8_t any = 0;
    if(addChild(nodeIdx, bPos[0], bP[0]) != 0xFF) {
        any = 1;
#ifdef WIDEN_PROBE
        wpAdded++;
#endif
        for(uint8_t k = 1; k < WIDEN_BATCH; k++) {
            if(bPos[k] == 0xFF) continue;
            if(addChild(nodeIdx, bPos[k] | 0x80, bP[k]) == 0xFF) break;
#ifdef WIDEN_PROBE
            wpAdded++;
#endif
        }
    }
    return any;
}

// Bitwise integer sqrt: isqrt32(x*2^24)/4096 approximates sqrt(x)
__attribute__((optimize("O2")))
static uint16_t isqrt32(uint32_t x) {
    uint32_t res = 0;
    // Start `bit` at the top power-of-4 of x's highest non-zero byte, so
    // the refine loop below only shifts within that byte (<=3 steps) rather
    // than walking down from 2^30. Each start is a power of 4 >= the largest
    // 4^k <= x, so the loop lands on the identical bit -- result unchanged.
    // Byte extraction (x >> 24 etc. are register renames on AVR) makes the
    // span tests single-byte tst instead of the 32-bit mask+or chains gcc
    // emits for `x & 0xFF000000UL`. `n` counts the remaining refine steps:
    // log4(bit)+1 at each start, decremented with the pre-loop lowering, so
    // the refine loop terminates on a dec/brne instead of a 4-byte bit!=0
    // compare. Same arithmetic in the same order -- bit-identical results.
    uint32_t bit;
    uint8_t n;
    if     ((uint8_t)(x >> 24)) { bit = 1UL << 30; n = 16; }
    else if((uint8_t)(x >> 16)) { bit = 1UL << 22; n = 12; }
    else if((uint8_t)(x >>  8)) { bit = 1UL << 14; n = 8;  }
    else                        { bit = 1UL << 6;  n = 4;  }
    while(bit > x) { bit >>= 2; n--; }
    for(; n; n--) {
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

// Q12 natural-log fractional part: log2(1 + i/16) already scaled by
// ln2 (2839 in Q12). Folding ln2 into the table lets lnQ12 avoid a
// 32-bit multiply (see below).
PROGMEM const uint16_t LN_FRAC[16] = {
    0, 248, 482, 704, 914, 1113, 1304, 1486,
    1660, 1827, 1988, 2143, 2292, 2435, 2574, 2708
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
    // ln x = k*ln2 + frac; bit-identical to the old (log2 << 12) * ln2
    // >> 12 (the k<<12 term times ln2 >> 12 is exactly k*2839, and the
    // fraction floors independently), with the 32-bit multiply gone.
    return (uint16_t)k * 2839 + pgm_read_word(LN_FRAC + frac);
}

// (w<<6)/v for a win/visit pair, i.e. the Q6 win rate. Because w<=v the
// quotient is <=64 (7 bits), so a 7-step restoring divide replaces libgcc's
// full 16-bit __udivmodhi4 (~2x cheaper) and is bit-exact. Valid while
// v<<6 fits u16 (v<=1023); real visit counts stay far under that at the
// shipped 400-iteration budget, and POISONED children have w==0 -> 0.
__attribute__((optimize("O2")))
static inline uint16_t winRate6(uint16_t w, uint16_t v) {
    uint16_t num = w << 6, d = v << 6, q = 0;
    for(uint8_t bit = 64; bit; bit >>= 1) {
        if(num >= d) { num -= d; q |= bit; }
        d >>= 1;
    }
    return q;
}

// Gelly-Silver RAVE ratio (RAVE_K<<12)/(3nv+RAVE_K). Factor exactly to
// 409600/(nv+100) -- 1228800 = 3*409600, so the floors match -- whose
// quotient is <=4055 (12-bit). A 12-step restoring divide then replaces
// libgcc's full 32-bit __udivmodsi4. -O2 so the loop unrolls.
__attribute__((optimize("O2")))
static inline uint16_t raveRatio(uint16_t nv) {
    uint32_t num = 409600UL, dd = (uint32_t)(nv + 100) << 11;
    uint16_t ratio = 0;
    for(uint16_t bit = 0x800; bit; bit >>= 1) {
        if(num >= dd) { num -= dd; ratio |= bit; }
        dd >>= 1;
    }
    return ratio;
}

static uint8_t selectChild(uint8_t nodeIdx) {
    // UCB1-Tuned in Q12 fixed point — software floats cost several ms
    // per root scan, so everything here is integer.
    // +1: the parent may have no real visits yet on its first selection.
    uint16_t lnN = lnQ12(nVisits(nodeIdx) + 1);

    uint8_t atRoot = (nodeIdx == 0);
    uint16_t best = 0;
    // fallback: first NON-latent child (a latent fallback once leaked
    // move|0x80 into simPlay = out-of-bounds board write)
    uint8_t bestC = node(nodeIdx).firstChild;
    while(bestC != 0xFF && (node(bestC).move & 0x80))
        bestC = node(bestC).nextSibling;
    for(uint8_t c = node(nodeIdx).firstChild; c != 0xFF; c = node(c).nextSibling) {
        Node &n = node(c);
        if(n.move & 0x80) continue; // latent: not yet in the schedule
        uint16_t nv = nVisits(c);
#ifdef LATENT_DEBUG
        if(nv == 0) {
            fprintf(stderr,
                "ZERO-VISIT CHILD: c=%u move=%u parent=%u poolUsed=%u "
                "freeHead=%u firstChild=%u nextSib=%u\n",
                c, n.move, nodeIdx, poolUsed, freeHead,
                n.firstChild, n.nextSibling);
            abort();
        }
#endif
        // Q6 win rate via a 16-bit divide (wins < 1024 so wins<<6 fits
        // uint16): (wins<<6)/nv == (wins<<12)/nv >> 6 exactly, so q is
        // the old Q12 value with its low 6 bits zeroed. That truncation
        // is well under the sampling noise (>=2.5% even at the root).
        uint16_t q6 = winRate6(nWins(c), nv);
        uint16_t q = q6 << 6;                 // Q12 (Q6 precision)

        // Variance-aware exploration from the raw win rate: with binary
        // rewards the sample variance is just q(1-q). Q6*Q6 = Q12, so
        // q6*(64-q6) is the SAME Q12 variance as (q*(4096-q))>>12 but a
        // 16-bit multiply instead of a 32-bit one.
        uint16_t lnOverN = lnN / nv;
        // The confidence term sqrt(2 lnN/n_j) alone saturates the min(1/4, .)
        // cap once 2*lnOverN >= 256 (lnOverN >= 128), because isqrt32(256<<12)
        // == 1024. So for those children v is provably 1024 — skip the isqrt
        // AND the variance. Only near-root children (n_j > lnN/128) need the
        // full form. The <32768 guard preserves the existing 2*lnOverN 16-bit
        // wrap for the rare n_j==1 / high-lnN case.
        uint32_t v;
        if(lnOverN >= 128 && lnOverN < 32768) {
            v = 1024;
        } else {
            v = (uint16_t)(q6 * (64 - q6));
            v += isqrt32((uint32_t)(2 * lnOverN) << 12);
            if(v > 1024) v = 1024; // min(1/4, ...)
        }

        // Root RAVE blend, Gelly-Silver β from the CHILD's visits:
        // fresh children lean on AMAF evidence, established children
        // graduate to their own record. (β from the parent's count
        // kept even a 130-visit leader half-AMAF and flattened the
        // root.) Never lift a poisoned (illegal) child back via RAVE.
        if(atRoot && nv < POISONED &&
           n.move < BOARD_CELLS && raveV[n.move]) {
            uint16_t ratio = raveRatio(nv);
            uint16_t beta = isqrt32((uint32_t)ratio << 12);
            uint16_t qr = winRate6(raveW[n.move], raveV[n.move]) << 6;
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
            if(childCount(cur) < maxKids) {
                // a stored latent fills the slot without a scan; the
                // FIRST flagged child in the walk is the best remaining
                // (worst-first adds on a prepend list = best-first walk)
                uint8_t lat = latentBest(cur);
                if(lat != 0xFF)
                    node(lat).move &= 0x7F;
                else if(allocReady())
                    widenNode(cur, toMove, ko, lastMove);
            }
        }
#ifdef LATENT_DEBUG
        if(path[0] != 0) { fprintf(stderr,"CANARY post-trigger cur=%u pd=%u\n",cur,pathDepth); abort(); }
#endif
        uint8_t c = selectChild(cur);
#ifdef LATENT_DEBUG
        if(path[0] != 0) { fprintf(stderr,"CANARY post-select cur=%u c=%u pd=%u\n",cur,c,pathDepth); abort(); }
#endif
        uint8_t nk = simPlay(node(c).move, toMove, ko);
#ifdef LATENT_DEBUG
        if(path[0] != 0) { fprintf(stderr,"CANARY post-simPlay c=%u pd=%u\n",c,pathDepth); abort(); }
#endif
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

    // True-komi result feeds the eval (resignation, passing, stats);
    // the tree and RAVE learn under the virtual komi (see vKomi2)
    thinkSims++;
    if(winner == rootTurn) thinkSimWins++;
#ifndef ARDUINO
    thinkMargin2Sum += (rootTurn == BLACK)
        ? (int16_t)(lastMargin2 - (int16_t)simKomi)
        : (int16_t)((int16_t)simKomi - lastMargin2);
#endif
    if(vKomi2) winner = vKomiWinner();

    // Fold this simulation into the root RAVE tables
    uint8_t rootWin = (winner == rootTurn);
    for(uint8_t i = 0; i < BOARD_CELLS; i++) {
        if(!(raveMask[i >> 3] & bitMask(i))) continue;
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
    // Endgame only (>= 45 stones, same bar as the settled-territory
    // pass): passing back ends the game and hands it to scoreDead's
    // vote, which on an OPEN board is coin flips, not a count — the
    // naive check below used to fire on a 6-stone board ("winning" by
    // bare komi) and pass a move-6 game into a random scoring.
    if(game.consecutivePasses == 1 && countStones(game) >= 45) {
        // Don't trust the count if the previous search already read
        // this game as bad (under ~30%): dead stones make
        // computeScore miscount in both directions, and a massacre
        // position full of our corpses can neutralize enough enemy
        // territory to read as a "win" — passing then gifts the game.
        // Floor raised 30% -> 55%: the static count this pass relies
        // on scores dead-but-eyespaced groups as alive (see
        // scoreDead), so near-even evals must keep playing instead
        // of passing into a miscounted "win".
        uint8_t evalOK = !thinkSims ||
            (uint32_t)thinkSimWins * 20 >= (uint32_t)thinkSims * 11;
        if(evalOK) {
            game.computeScore();
            if(game.winner() == game.turn) {
                passToWin = 1;
                resignCount = 0;
                return;
            }
        }
        // Ownership-corrected settle gate (endgame only). The naive
        // count above scores dead stones as alive and trusts a stale
        // eval; the scoreDead-style vote reads the board honestly —
        // and it is the SAME vote game-over scoring applies, so a pass
        // taken here scores the way the vote says. Clearly winning →
        // pass and bank it (catches wins the naive count undercounts
        // and the evalOK gate skips). Clearly lost → pass and accept
        // the count instead of flailing stones into groups the vote
        // already reads as dead. Contested → play on.
        int16_t m2c = settleVote(game);
        if(m2c != SETTLE_NONE &&
           (m2c > 0 || m2c <= -SETTLE_ACCEPT2)) {
            passToWin = 1;
            resignCount = 0;
            return;
        }
    }

    poolUsed = 0;
    freeHead = 0xFF;
    thinkSims = thinkSimWins = 0;
#ifndef ARDUINO
    thinkMargin2Sum = 0;
#endif
    rootTurn = game.turn;
    simKomi = game.kpieces;
#ifndef ARDUINO
    // Host: fresh libc-derived state per think, downstream of the
    // harness's per-run srand. Device state free-runs from boot.
    // forceThinkSeed (0 = off) replays a recorded seed; lastThinkSeed
    // captures the seed used so a saved game reproduces move-for-move.
    rngState = forceThinkSeed ? forceThinkSeed : (random(0xFFFF) | 1);
    lastThinkSeed = rngState;
#endif

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

    loadRootBoard(game);

    memset(raveV, 0, BOARD_CELLS);
    memset(raveW, 0, BOARD_CELLS);

    newNode(0xFF); // root
    uint16_t iters = mctsIterations;
    if(rootStones < OPENING_BOOST_STONES) iters += iters / 2;
    uint16_t total = iters;
    uint8_t extended = 0;
#ifdef DECIDE_PROBE
    dpLastChange = 0; dpPrevBest = 0xFF;
#endif
    for(uint16_t i = 0; i < total; i++) {
#ifndef ARDUINO
        thinkItersRun++;
#endif
        mctsIterate(game);
#ifdef DECIDE_PROBE
        {
            uint16_t bv = 0; uint8_t bc = 0xFF;
            for(uint8_t c = node(0).firstChild; c != 0xFF; c = node(c).nextSibling) {
                uint16_t v = nVisits(c);
                if(v < POISONED && v > bv) { bv = v; bc = c; }
            }
            if(bc != dpPrevBest) { dpPrevBest = bc; dpLastChange = i; }
        }
#endif
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
#ifdef FASTPLAY_STOP
            uint8_t topC = 0xFF;
#endif
            for(uint8_t c = node(0).firstChild; c != 0xFF; c = node(c).nextSibling) {
                uint16_t v = nVisits(c);
                if(v >= POISONED) continue;
                if(v > top1) {
                    top2 = top1; top1 = v;
#ifdef FASTPLAY_STOP
                    topC = c;
#endif
                }
                else if(v > top2) top2 = v;
            }
            if(top1 - top2 > total - 1 - i) break;
#ifdef FASTPLAY_STOP
            // michi's FASTPLAY: a decisively winning leader ends the
            // search early (their 5%/20% sim tiers at 0.95/0.80,
            // adapted to our budget). The 32-real-visit floor keeps
            // seeded q honest: at (14,10) max seed, faking q>=0.95
            // needs more wins than real visits -- impossible.
            // (dose 1 with michi's second tier -- wr>0.80 at 25% of
            // budget -- lost -6.4pp/1000: at 400 iters the 25% leader
            // has ~40 visits and 0.80 has a +-0.12 CI; it stopped in
            // merely-good games and blew won ones. 0.95-only retry.)
            if(topC != 0xFF && top1 >= 32 && i >= total / 8) {
                uint16_t q6 = winRate6(nWins(topC), top1);
                if(q6 >= 61) break;                  // wr > 0.95
            }
#endif
        }
    }

#ifndef ARDUINO
    thinkItersBudget += total;
    thinkAvgMargin2 = thinkSims
        ? (int16_t)(thinkMargin2Sum / (int32_t)thinkSims) : 0;
#endif

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

    // Dynamic-komi adaptation for the NEXT search (see vKomi2):
    // clearly losing -> ask for two points less, so the tree fights
    // for margin instead of miracle collapses; healthy -> tighten
    // back toward the real game. The cap keeps truly lost games
    // reading lost, so resignation still fires on the true eval.
    if(thinkSims) {
        uint32_t w20 = (uint32_t)thinkSimWins * 20;
        if(w20 < (uint32_t)thinkSims * 7) {
            if(vKomi2 < VKOMI_MAX2) vKomi2 += VKOMI_STEP2;
        } else if(w20 > (uint32_t)thinkSims * 11 && vKomi2) {
            vKomi2 -= VKOMI_STEP2;
        }
    }
}

// Root self-atari veto: a move that leaves its own merged group at
// one liberty while capturing nothing is a gift stone. The prior
// penalty discourages it, but playouts approve the "trap" whenever
// the answering side fumbles locally — a real game shipped one
// (7/10 seeds reproduced it). The root choice skips such children
// and the LCB race falls through to the next-best. Cost: the rare
// legitimate throw-in tesuji, which this engine never follows up
// anyway. Reads the root position from simBoard.
__attribute__((noinline))
static uint8_t rootSelfAtari(uint8_t pos, uint8_t toMove) {
    simBoard[pos] = toMove;
    uint8_t r = 0;
    if((uint8_t)groupLibsCore(pos, 0, 2) <= 1) {
        r = 1;
        // ...unless the stone captures: with it placed, a doomed
        // enemy neighbor chain reads zero liberties
        uint8_t q;
        FOR_EACH_NEIGHBOR(q, pos)
            if(simBoard[q] == 3 - toMove && !hasLiberty(q)) {
                r = 0;
                break;
            }
    }
    simBoard[pos] = EMPTY;
    return r;
}

__attribute__((noinline))
static uint8_t pct100(uint16_t w, uint16_t n) {
    return n ? (uint8_t)((uint32_t)w * 100 / n) : 0;
}

uint8_t AI::bestMove(Game &game, uint8_t &x, uint8_t &y) {
    // Stats default to the whole-root eval; the chosen child's own
    // numbers overwrite them below once it is known
    statTotal = thinkSims;
    statVisits = 0;
    statPct = pct100(thinkSimWins, thinkSims);

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
    uint8_t bestC = 0xFF, backC = 0xFF;
    for(uint8_t c = node(0).firstChild; c != 0xFF; c = node(c).nextSibling) {
        uint16_t v = nVisits(c);
        if(v < POISONED && v > maxV) maxV = v;
    }
    // Root position for rootSelfAtari and the settled-territory check
    for(uint8_t i = 0; i < BOARD_CELLS; i++)
        simBoard[i] = packedGet(game.board, i);
    for(uint8_t c = node(0).firstChild; c != 0xFF; c = node(c).nextSibling) {
        uint16_t v = nVisits(c);
        if(v >= POISONED) continue;
        if(node(c).move & 0x80) continue; // latent: never activated
        uint8_t m = node(c).move;

        // Fallback: most-visited, in case no child clears the LCB gate
        if(v > backV) {
            if(m == MOVE_PASS ||
               (game.isValidMove(m % BOARD_SIZE, m / BOARD_SIZE) &&
                !rootSelfAtari(m, game.turn))) {
                backV = v;
                backup = m;
                backC = c;
            }
        }

        // Gate: prior-seeded children carry inflated q at tiny n and
        // would fake a strong LCB — demand real sampling first
        if(v < LCB_GATE || v * LCB_REL_DIV < maxV) continue;

        uint16_t q6 = (nWins(c) << 6) / v;    // Q6 win rate, 16-bit divide
        uint16_t q = q6 << 6;                 // Q12 (Q6 precision)
        uint32_t var = (uint16_t)(q6 * (64 - q6)); // q(1-q), Q12, 16-bit mul
        // (var<<12)/v is Q24 of q(1-q)/n, so isqrt lands in Q12
        uint16_t term = isqrt32(((uint32_t)var << 12) / v);
        int16_t lcb = (int16_t)q - (int16_t)(((uint32_t)term * LCB_Z) >> 8);
        if(lcb <= bestL) continue;
        if(m != MOVE_PASS &&
           (!game.isValidMove(m % BOARD_SIZE, m / BOARD_SIZE) ||
            rootSelfAtari(m, game.turn)))
            continue;
        bestL = lcb;
        best = m;
        bestC = c;
    }
    if(best == MOVE_PASS) { best = backup; bestC = backC; }
    if(bestC != 0xFF) {
        statVisits = nVisits(bestC);
        if(statVisits) statPct = pct100(nWins(bestC), statVisits);
    }
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
    // Endgame only (>= 45 stones) and only with the search agreeing
    // we are winning (55%, same floor as passToWin): early on the
    // whole empty space is ONE mixed region, so the static count
    // "wins" by bare komi from move zero — and a noise favorite
    // poking a 1-point enemy pocket once turned that into a
    // mid-game free pass (real game, move 18).
    // best lands in a region bordered by a single colour -- our own territory
    // (own fill: -1 pt) or a hopeless invasion of theirs (gifts a prisoner) --
    // of ANY size (settledRegionColor is uncapped, so large territories that
    // regionVital drops as "too open" are caught). Still defer to a small
    // killable region's vital point, which is a real life-and-death move.
    // simBoard still holds the root position from above
    uint16_t rv;
    if(rootStones >= 45 &&
       (uint32_t)thinkSimWins * 20 >= (uint32_t)thinkSims * 11 &&
       settledRegionColor(best) &&
       !((uint8_t)(rv = regionVital(best)) && (rv >> 8) == best)) {
        game.computeScore();
        if(game.winner() == game.turn) return 0;
    }

    x = best % BOARD_SIZE;
    y = best / BOARD_SIZE;
    return 1;
}
