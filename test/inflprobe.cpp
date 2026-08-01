
// Influence-estimator agreement probe (v2): truth playouts with board
// snapshots; influence verdicts at snapshots + final; margin debug.
#include <cstdio>
#include <cstring>
#include <cstdlib>
#define PLAYOUT_SNAP
#include "../game.cpp"
#include "../ai.cpp"
uint8_t Arduboy2Base::sBuffer[1024];
static Game game;

static int16_t infl[81], infl2[81];
static int16_t inflMargin2(const uint8_t *bd){
  for(uint8_t i=0;i<81;i++)
    infl[i] = bd[i]==BLACK ? 64 : bd[i]==WHITE ? -64 : 0;
  for(uint8_t pass=0; pass<3; pass++){
    for(uint8_t i=0;i<81;i++){
      int16_t s=0; uint8_t q;
      FOR_EACH_NEIGHBOR(q,i){
        if(infl[q]>0) s++;
        else if(infl[q]<0) s--;
      }
      infl2[i] = infl[i] + s*8;
    }
    memcpy(infl,infl2,sizeof(infl));
  }
  int16_t m2=0;
  for(uint8_t i=0;i<81;i++){
    if(infl[i]>0) m2+=2; else if(infl[i]<0) m2-=2;
  }
  return m2;
}

static const char *SGF =
 "B[ed]W[dc]B[dd]W[cc]B[ef]W[fb]B[df]W[gd]B[ge]W[he]B[gf]W[hf]B[gg]W[be]"
 "B[ec]W[cg]B[gh]W[fd]B[fc]W[gc]B[db]W[eb]B[cb]W[fe]B[bf]W[cf]B[bg]W[cd]"
 "B[bc]W[bd]B[bb]W[bh]B[hd]W[eh]B[ff]W[ih]B[gb]W[hb]B[hc]W[ga]";

int main(int argc,char**argv){
  int nmoves = argc>1 ? atoi(argv[1]) : 24;
  int dbg = argc>2;
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
  int n=0, aF=0,a40=0,a60=0,a80=0, r40=0,r60=0,r80=0;
  int truthB=0, i60B=0, i60n=0;
  for(int k=0;k<2000;k++){
    uint16_t seed=(uint16_t)((unsigned)k*2654435761u>>13)|1;
    memset(plSnap40,0xEE,81); memset(plSnap60,0xEE,81); memset(plSnap80,0xEE,81);
    rngState=seed;
    memcpy(simBoard,root,81);
    uint8_t truth=playout(game.turn,0xFF,0xFF);
    int16_t trueM2=lastMargin2;
    n++;
    int16_t im=inflMargin2(simBoard);
    if((im > (int16_t)simKomi ? BLACK : WHITE)==truth) aF++;
    else if(dbg && n<20)
      printf("  dbg: true margin2=%d infl margin2=%d komi2=%d truth=%u\n",
             trueM2, im, (int)simKomi, truth);
    if(plSnap40[0]!=0xEE){ r40++;
      if((inflMargin2(plSnap40) > (int16_t)simKomi ? BLACK : WHITE)==truth) a40++; }
    if(plSnap60[0]!=0xEE){ r60++;
      uint8_t iw = inflMargin2(plSnap60) > (int16_t)simKomi ? BLACK : WHITE;
      if(iw==truth) a60++;
      i60n++; if(iw==BLACK) i60B++; if(truth==BLACK) truthB++; }
    if(plSnap80[0]!=0xEE){ r80++;
      if((inflMargin2(plSnap80) > (int16_t)simKomi ? BLACK : WHITE)==truth) a80++; }
  }
  printf("after %d game-moves: final %.1f%%", nmoves, 100.0*aF/n);
  if(r40) printf("   @40 %.1f%% (n=%d)", 100.0*a40/r40, r40);
  if(r60) printf("   @60 %.1f%% (n=%d)", 100.0*a60/r60, r60);
  if(r80) printf("   @80 %.1f%% (n=%d)", 100.0*a80/r80, r80);
  printf("\n");
  if(i60n)
    printf("  bias @60: truth Black-win %.1f%%  influence Black-win %.1f%%  (delta %+.1f)\n",
           100.0*truthB/i60n, 100.0*i60B/i60n, 100.0*(i60B-truthB)/i60n);
  return 0;
}
