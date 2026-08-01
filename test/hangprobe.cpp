// freeze repro: replay hunt games to various depths, human passes,
// engine thinks. Prints progress so a hang pinpoints its position.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <utility>
#include "../game.cpp"
#include "../ai.cpp"
uint8_t Arduboy2Base::sBuffer[1024];
int main(int argc, char **argv) {
    char path[512];
    int n = 0;
    while(fgets(path, sizeof(path), stdin)) {
        std::string p(path);
        while(!p.empty() && (p.back() == '\n' || p.back() == ' ')) p.pop_back();
        FILE *f = fopen(p.c_str(), "r");
        if(!f) continue;
        std::string sgf;
        char buf[4096]; size_t r;
        while((r = fread(buf, 1, sizeof(buf), f)) > 0) sgf.append(buf, r);
        fclose(f);
        std::vector<std::pair<uint8_t,uint8_t>> mv;
        for(size_t i = 0; i + 5 < sgf.size(); i++)
            if(sgf[i] == ';' && (sgf[i+1] == 'B' || sgf[i+1] == 'W') && sgf[i+2] == '[') {
                if(sgf[i+3] == ']') mv.push_back({0xFF, 0xFF});
                else mv.push_back({(uint8_t)(sgf[i+3]-'a'), (uint8_t)(sgf[i+4]-'a')});
            }
        if(mv.size() < 30) continue;
        for(int depth : {12, 21, 30, 39, 48}) {
            if((size_t)depth >= mv.size()) break;
            Game game; AI ai;
            game.reset(); ai.reset();
            for(int m = 0; m < depth; m++) {
                if(mv[m].first == 0xFF) { game.pass(); ai.notifyPass(); }
                else { game.playMove(mv[m].first, mv[m].second); ai.notifyMove(mv[m].first, mv[m].second); }
            }
            game.pass();  // the human passes here
            ai.notifyPass();
            printf("game %s depth %d think...", p.c_str() + p.size() - 12, depth);
            fflush(stdout);
            ai.think(game);
            uint8_t x, y;
            uint8_t plays = ai.bestMove(game, x, y);
            printf(" ok (%s)\n", plays ? "plays" : "PASSES");
            fflush(stdout);
        }
        if(++n >= 12) break;
    }
    printf("done, no hang\n");
    return 0;
}
