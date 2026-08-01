// Fullness early-stop probe: does the ADJACENCY scorer at empties<=E
// agree with the played-out truth, and how many moves would it save?
#include <cstdio>
#include <cstring>
#include <cstdlib>
#define PLAYOUT_SNAP
#define PLAYOUT_SNAP_EMPT
#define PLAYOUT_STATS
#include "../game.cpp"
#include "../ai.cpp"
uint8_t Arduboy2Base::sBuffer[1024];
static Game game;

static int16_t adjMargin2(const uint8_t *bd){
  int16_t black=0, white=0;
  for(uint8_t i=0;i<81;i++){
    if(bd[i]==BLACK){black++;continue;}
    if(bd[i]==WHITE){white++;continue;}
    uint8_t tb=0,tw=0,q;
    FOR_EACH_NEIGHBOR(q,i){
      if(bd[q]==BLACK)tb=1;
      if(bd[q]==WHITE)tw=1;
    }
    if(tb&&!tw)black++; else if(tw&&!tb)white++;
  }
  return (int16_t)black*2-(int16_t)white*2;
}

static const char *SGF =
 "B[ed]W[dc]B[dd]W[cc]B[ef]W[fb]B[df]W[gd]B[ge]W[he]B[gf]W[hf]B[gg]W[be]"
 "B[ec]W[cg]B[gh]W[fd]B[fc]W[gc]B[db]W[eb]B[cb]W[fe]B[bf]W[cf]B[bg]W[cd]"
 "B[bc]W[bd]B[bb]W[bh]B[hd]W[eh]B[ff]W[ih]B[gb]W[hb]B[hc]W[ga]";

int main(int argc,char**argv){
  int nmoves = argc>1 ? atoi(argv[1]) : 24;
  game.reset();
  const char *p=SGF; uint8_t color=BLACK;
  for(int i=0;i<nmoves;i++){
    p=strchr(p,'[')+1;
    game.set(p[0]-'a',p[1]-'a',color); color=3-color; p+=2;
  }
  game.turn=color;
  simKomi=game.kpieces; rootTurn=game.turn; scoreMode=0;
  uint8_t root[81];
  for(uint8_t i=0;i<81;i++) root[i]=packedGet(game.board,i);
  int n=0;
  int agree[4]={0,0,0,0}, reach[4]={0,0,0,0};
  long savedMoves[4]={0,0,0,0};
  int truthB=0, estB[4]={0,0,0,0};
  long totalLen=0;
  for(int k=0;k<2000;k++){
    uint16_t seed=(uint16_t)((unsigned)k*2654435761u>>13)|1;
    memset(plSnapMove,0xFF,4);
    plN=plMoves=0; plEndCap=plEndPass=plEndMercy=0;
    rngState=seed;
    memcpy(simBoard,root,81);
    uint8_t truth=playout(game.turn,0xFF,0xFF);
    uint16_t len=plMoves;
    totalLen+=len;
    n++; if(truth==BLACK) truthB++;
    for(int t=0;t<4;t++){
      if(plSnapMove[t]==0xFF) continue;
      reach[t]++;
      savedMoves[t]+=(len>plSnapMove[t])?len-plSnapMove[t]:0;
      int16_t m2=adjMargin2(plSnapE[t]);
      uint8_t w = m2 > (int16_t)simKomi ? BLACK : WHITE;
      if(w==truth) agree[t]++;
      if(w==BLACK) estB[t]++;
    }
  }
  printf("after %d game-moves (avg playout len %.1f):\n", nmoves, (double)totalLen/n);
  for(int t=0;t<4;t++){
    if(!reach[t]) continue;
    printf("  empties<=%2u: reached %4d/%d  agree %5.1f%%  bias %+5.1fpp  avg moves saved %.1f\n",
      plSnapThresh[t], reach[t], n, 100.0*agree[t]/reach[t],
      100.0*(estB[t]-(long)truthB*reach[t]/n)/reach[t],
      (double)savedMoves[t]/reach[t]);
  }
  return 0;
}
