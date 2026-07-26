// Clickable macOS ArduGo tester: the real device AI (game.cpp/ai.cpp
// compiled in, Arduino headers stubbed) behind a native Cocoa board.
// No dependencies beyond the Cocoa framework. Build: make play_gui
#import <Cocoa/Cocoa.h>

#include <vector>
#include <time.h>

#include "../game.cpp"
#include "../ai.cpp"

uint8_t Arduboy2Base::sBuffer[1024];

static Game game;
static AI ai;
static int lastPos = -1;
static uint8_t humanColor = BLACK;
static BOOL thinking = NO;

struct Snap { Game g; AI a; int last; };
static std::vector<Snap> history;

static const CGFloat kMargin = 36;
static const CGFloat kCell = 46;
static const CGFloat kBoardSpan = kMargin * 2 + kCell * 8;
static const CGFloat kBarH = 40;

@class BoardView;
static BoardView *gBoard;
static NSWindow *gWindow;

static NSString *vertexName(int pos) {
    char col = 'A' + pos % BOARD_SIZE;
    if(col >= 'I') col++;
    return [NSString stringWithFormat:@"%c%d", col, 9 - pos / BOARD_SIZE];
}

static void setStatus(NSString *s) {
    gWindow.title = [NSString stringWithFormat:@"ArduGo — %@", s];
}

static void showScore() {
    game.computeScore();
    int b2 = game.territory[0] * 2 + game.captures[0] * 2;
    int w2 = game.territory[1] * 2 + game.captures[1] * 2 + game.kpieces;
    setStatus([NSString stringWithFormat:@"%s WINS  B %d+%d=%d  W %d+%d+6.5=%d.5",
               game.winner() == BLACK ? "BLACK" : "WHITE",
               game.territory[0], game.captures[0], b2 / 2,
               game.territory[1], game.captures[1], w2 / 2]);
}

static void aiMoveIfNeeded();

@interface BoardView : NSView
@end

@implementation BoardView
- (BOOL)isFlipped { return YES; }

- (void)drawRect:(NSRect)dirty {
    [[NSColor colorWithCalibratedRed:0.87 green:0.72 blue:0.45 alpha:1] setFill];
    NSRectFill(self.bounds);

    [[NSColor blackColor] setStroke];
    for(int i = 0; i < BOARD_SIZE; i++) {
        CGFloat o = kMargin + i * kCell;
        NSBezierPath *p = [NSBezierPath bezierPath];
        [p moveToPoint:NSMakePoint(kMargin, o)];
        [p lineToPoint:NSMakePoint(kMargin + 8 * kCell, o)];
        [p moveToPoint:NSMakePoint(o, kMargin)];
        [p lineToPoint:NSMakePoint(o, kMargin + 8 * kCell)];
        [p setLineWidth:1];
        [p stroke];
    }

    // Hoshi
    const int hoshi[][2] = {{2,2},{6,2},{4,4},{2,6},{6,6}};
    [[NSColor blackColor] setFill];
    for(auto &h : hoshi) {
        NSRect r = NSMakeRect(kMargin + h[0] * kCell - 3.5,
                              kMargin + h[1] * kCell - 3.5, 7, 7);
        [[NSBezierPath bezierPathWithOvalInRect:r] fill];
    }

    // Coordinates
    NSDictionary *attrs = @{ NSFontAttributeName: [NSFont systemFontOfSize:11],
                             NSForegroundColorAttributeName: [NSColor colorWithWhite:0.25 alpha:1] };
    for(int i = 0; i < BOARD_SIZE; i++) {
        char col = 'A' + i;
        if(col >= 'I') col++;
        NSString *cs = [NSString stringWithFormat:@"%c", col];
        [cs drawAtPoint:NSMakePoint(kMargin + i * kCell - 4, kMargin + 8 * kCell + 12)
             withAttributes:attrs];
        NSString *rs = [NSString stringWithFormat:@"%d", 9 - i];
        [rs drawAtPoint:NSMakePoint(kMargin - 24, kMargin + i * kCell - 7)
             withAttributes:attrs];
    }

    // Stones
    for(int y = 0; y < BOARD_SIZE; y++)
        for(int x = 0; x < BOARD_SIZE; x++) {
            uint8_t c = game.at(x, y);
            if(c == EMPTY) continue;
            NSRect r = NSMakeRect(kMargin + x * kCell - kCell * 0.45,
                                  kMargin + y * kCell - kCell * 0.45,
                                  kCell * 0.9, kCell * 0.9);
            NSBezierPath *s = [NSBezierPath bezierPathWithOvalInRect:r];
            [(c == BLACK ? [NSColor blackColor] : [NSColor whiteColor]) setFill];
            [s fill];
            [[NSColor blackColor] setStroke];
            [s stroke];
            if(y * BOARD_SIZE + x == lastPos) {
                NSRect m = NSMakeRect(kMargin + x * kCell - 5,
                                      kMargin + y * kCell - 5, 10, 10);
                [[NSColor systemRedColor] setStroke];
                NSBezierPath *ring = [NSBezierPath bezierPathWithOvalInRect:m];
                [ring setLineWidth:2];
                [ring stroke];
            }
        }
}

- (void)mouseDown:(NSEvent *)e {
    if(thinking || game.isGameOver() || game.resignedBy ||
       game.turn != humanColor) return;
    NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
    int gx = (int)lround((p.x - kMargin) / kCell);
    int gy = (int)lround((p.y - kMargin) / kCell);
    if(gx < 0 || gx >= BOARD_SIZE || gy < 0 || gy >= BOARD_SIZE) return;
    if(fabs(p.x - (kMargin + gx * kCell)) > kCell * 0.45 ||
       fabs(p.y - (kMargin + gy * kCell)) > kCell * 0.45) return;

    history.push_back({game, ai, lastPos});
    if(!game.playMove(gx, gy)) {
        history.pop_back();
        NSBeep();
        return;
    }
    ai.notifyMove(gx, gy);
    lastPos = gy * BOARD_SIZE + gx;
    [self setNeedsDisplay:YES];
    aiMoveIfNeeded();
}
@end

static void aiMoveIfNeeded() {
    if(game.isGameOver()) { showScore(); return; }
    if(game.turn == humanColor) {
        setStatus([NSString stringWithFormat:@"your move (%s)",
                   humanColor == BLACK ? "black" : "white"]);
        return;
    }
    thinking = YES;
    setStatus(@"AI thinking…");
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        uint8_t before[PACKED_BOARD_BYTES];
        memcpy(before, game.board, sizeof before);
        NSString *info;
        if(ai.chooseMove(game)) {
            for(uint8_t i = 0; i < BOARD_CELLS; i++)
                if(packedGet(game.board, i) != EMPTY &&
                   packedGet(before, i) == EMPTY) lastPos = i;
            info = [NSString stringWithFormat:@"AI: %@ (book)", vertexName(lastPos)];
        } else {
            clock_t t0 = clock();
            ai.think(game);
            int ms = (int)((clock() - t0) * 1000 / CLOCKS_PER_SEC);
            uint8_t x, y;
            if(ai.resigned) {
                game.resignedBy = 3 - humanColor;
                info = @"AI RESIGNS — you win!  (New Game to play again)";
            } else if(ai.bestMove(game, x, y)) {
                game.playMove(x, y);
                ai.notifyMove(x, y);
                lastPos = y * BOARD_SIZE + x;
                // Leader stats for the title readout
                uint16_t bestV = 0, bestW = 0;
                for(uint8_t c = node(0).firstChild; c != 0xFF; c = node(c).nextSibling) {
                    uint16_t v = nVisits(c);
                    if(v >= POISONED || v <= bestV) continue;
                    bestV = v;
                    bestW = nWins(c);
                }
                info = [NSString stringWithFormat:
                        @"AI: %@  [%d ms]  position ~%d%%  move %d%%",
                        vertexName(lastPos), ms,
                        thinkSims ? (int)(100L * thinkSimWins / thinkSims) : 50,
                        bestV ? (int)(100L * bestW / bestV) : 0];
            } else {
                game.pass();
                ai.notifyPass();
                lastPos = -1;
                info = @"AI passes";
            }
        }
        dispatch_async(dispatch_get_main_queue(), ^{
            thinking = NO;
            [gBoard setNeedsDisplay:YES];
            if(game.isGameOver()) showScore();
            else setStatus(info);
        });
    });
}

static void startNewGame() {
    game.reset();
    ai.reset();
    history.clear();
    lastPos = -1;
    [gBoard setNeedsDisplay:YES];
    aiMoveIfNeeded();
}

@interface Controller : NSObject <NSApplicationDelegate>
@end

@implementation Controller
- (void)pass:(id)s {
    if(thinking || game.isGameOver() || game.resignedBy ||
       game.turn != humanColor) return;
    history.push_back({game, ai, lastPos});
    game.pass();
    ai.notifyPass();
    lastPos = -1;
    [gBoard setNeedsDisplay:YES];
    aiMoveIfNeeded();
}

- (void)undo:(id)s {
    if(thinking || history.empty()) return;
    game = history.back().g;
    ai = history.back().a;
    lastPos = history.back().last;
    history.pop_back();
    [gBoard setNeedsDisplay:YES];
    setStatus(@"undone — your move");
}

- (void)newGame:(id)s {
    if(thinking) return;
    startNewGame();
}

- (void)swapSides:(id)s {
    if(thinking) return;
    humanColor = 3 - humanColor;
    [(NSButton *)s setTitle:humanColor == BLACK ? @"Play as White" : @"Play as Black"];
    startNewGame();
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)a {
    return YES;
}
@end

static NSButton *makeButton(NSString *title, CGFloat x, CGFloat w,
                            id target, SEL action, NSView *parent) {
    NSButton *b = [[NSButton alloc] initWithFrame:NSMakeRect(x, 6, w, 28)];
    b.title = title;
    b.bezelStyle = NSBezelStyleRounded;
    b.target = target;
    b.action = action;
    [parent addSubview:b];
    return b;
}

int main() {
    @autoreleasepool {
        srand((unsigned)time(NULL));
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

        Controller *ctl = [Controller new];
        [NSApp setDelegate:ctl];

        NSMenu *bar = [NSMenu new];
        NSMenuItem *appItem = [NSMenuItem new];
        [bar addItem:appItem];
        NSMenu *appMenu = [NSMenu new];
        [appMenu addItemWithTitle:@"Quit ArduGo" action:@selector(terminate:)
                    keyEquivalent:@"q"];
        appItem.submenu = appMenu;
        [NSApp setMainMenu:bar];

        NSRect content = NSMakeRect(0, 0, kBoardSpan, kBoardSpan + kBarH + 14);
        gWindow = [[NSWindow alloc]
            initWithContentRect:content
                      styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                NSWindowStyleMaskMiniaturizable
                        backing:NSBackingStoreBuffered
                          defer:NO];
        [gWindow center];

        gBoard = [[BoardView alloc]
            initWithFrame:NSMakeRect(0, kBarH, kBoardSpan, kBoardSpan + 14)];
        [gWindow.contentView addSubview:gBoard];

        makeButton(@"Pass", 10, 70, ctl, @selector(pass:), gWindow.contentView);
        makeButton(@"Undo", 86, 70, ctl, @selector(undo:), gWindow.contentView);
        makeButton(@"New Game", 162, 100, ctl, @selector(newGame:), gWindow.contentView);
        makeButton(@"Play as White", 268, 120, ctl, @selector(swapSides:), gWindow.contentView);

        startNewGame();
        [NSApp activateIgnoringOtherApps:YES];
        [gWindow makeKeyAndOrderFront:nil];
        [NSApp run];
    }
    return 0;
}
