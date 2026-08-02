// Nakade vital-point probe: constructed prey-inside eyespaces.
// Build with -DNAKADE. Coordinates: idx = y*9+x, corner at (0,0).
#include <cstdio>
#include <cstring>
#include "../game.cpp"
#include "../ai.cpp"
uint8_t Arduboy2Base::sBuffer[1024];

static void reset() { memset(simBoard, EMPTY, sizeof(simBoard)); }
static void put(uint8_t color, const int *idx, int n) {
    for(int i = 0; i < n; i++) simBoard[idx[i]] = color;
}
static void probe(const char *name, uint8_t seed, int expect) {
    uint8_t seen[11]; memset(seen, 0, sizeof(seen));
    uint8_t v = nakadeVital(seed, seen);
    printf("%-44s vital=%3d expect=%3d  %s\n", name, v, expect,
           (int)v == expect ? "OK" : "*** MISMATCH ***");
}

int main() {
    // 1. Bulky-five: B prey a1,c1,a2,b2 (4 stones), vital empty b1(=1).
    //    W wall d1,e1,c2,d2,a3,b3,c3 (one 7-stone chain).
    {
        reset();
        const int prey[] = {0, 2, 9, 10};
        const int wall[] = {3, 4, 11, 12, 18, 19, 20};
        put(BLACK, prey, 4); put(WHITE, wall, 7);
        probe("bulky-five prey, vital b1", 1, 1);
    }
    // 2. Straight-5 space, prey b1,c1,d1, pockets a1+e1: ties -> none.
    {
        reset();
        const int prey[] = {1, 2, 3};
        const int wall[] = {5, 9, 10, 11, 12, 13, 14};
        put(BLACK, prey, 3); put(WHITE, wall, 7);
        probe("straight-5 two-pocket prey (alive)", 0, 0xFF);
    }
    // 3. Pyramid-four: prey arms b1, a2, c2 (three 1-stone chains),
    //    vital = center b2 (=10). Wall around.
    {
        reset();
        const int prey[] = {1, 9, 11};
        const int wall[] = {0, 2, 3, 12, 21, 20, 19, 18};
        put(BLACK, prey, 3); put(WHITE, wall, 8);
        probe("pyramid-four prey, vital b2", 10, 10);
    }
    // 4. No prey (plain settled territory) -> not nakade's domain.
    {
        reset();
        const int wall[] = {2, 11, 20, 19, 18};
        put(WHITE, wall, 5);
        probe("empty corner region, no prey", 0, 0xFF);
    }
    // 5. Contested: wall of both colors.
    {
        reset();
        const int prey[] = {0, 9};
        const int wallW[] = {2, 11, 19, 20};
        const int wallB2[] = {18, 27, 28, 29, 21};
        put(BLACK, prey, 2); put(WHITE, wallW, 4); put(BLACK, wallB2, 5);
        probe("two wall colors (contested)", 1, 0xFF);
    }
    // 6. Prey with an escape route (not enclosed): flood blows cap.
    {
        reset();
        const int prey[] = {0, 9};   // libs a?/b1... open board beyond
        const int wall[] = {2, 11, 20, 19};
        put(BLACK, prey, 2); put(WHITE, wall, 4);
        probe("prey not enclosed (open b3 side)", 1, 0xFF);
    }
    return 0;
}
