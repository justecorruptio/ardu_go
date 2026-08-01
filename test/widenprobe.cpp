// Widening-waste probe: calls, adds, empty scans, and how many added
// candidates end the think with <=1 visit (wasted scans).
#include <cstdio>
#include <cstring>
#include <cstdlib>
#define WIDEN_PROBE
#include "../game.cpp"
#include "../ai.cpp"
uint8_t Arduboy2Base::sBuffer[1024];
static Game game; static AI ai;
static const char *SGF =
 "B[ed]W[dc]B[dd]W[cc]B[ef]W[fb]B[df]W[gd]B[ge]W[he]B[gf]W[hf]B[gg]W[be]"
 "B[ec]W[cg]B[gh]W[fd]B[fc]W[gc]B[db]W[eb]B[cb]W[fe]B[bf]W[cf]B[bg]W[cd]"
 "B[bc]W[bd]B[bb]W[bh]B[hd]W[eh]B[ff]W[ih]B[gb]W[hb]B[hc]W[ga]";
int main(){
  printf("%-8s %7s %7s %7s %9s\n","phase","calls","added","empty","ALLOCFAIL");
  for(int nm=8; nm<=40; nm+=8){
    game.reset();
    const char *p=SGF; uint8_t color=BLACK;
    for(int i=0;i<nm;i++){ p=strchr(p,'[')+1; game.set(p[0]-'a',p[1]-'a',color); color=3-color; p+=2; }
    game.turn=color;
    long calls=0,added=0,empty=0,fails=0,lowKids=0,totKids=0;
    for(int s=1;s<=5;s++){
      ai.reset(); srand(s); rngState=(uint16_t)(s*7919)|1;
      wpCalls=wpAdded=wpEmpty=wpAllocFail=0;
      ai.think(game);
      calls+=wpCalls; added+=wpAdded; empty+=wpEmpty; fails+=wpAllocFail;
      // walk the whole pool: children with <=1 visit
      for(uint8_t n=0;n<poolUsed;n++){
        uint16_t v=nVisits(n);
        if(v>=POISONED) continue;
        totKids++;
        if(v<=1) lowKids++;
      }
    }
    printf("mv%-6d %7.0f %7.0f %7.0f %8.0f%%\n", nm,
      calls/5.0, added/5.0, empty/5.0, calls?100.0*fails/calls:0.0);
  }
  return 0;
}
