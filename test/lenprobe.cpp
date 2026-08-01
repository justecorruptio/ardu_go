// Playout-ending distribution from three phases of a real game.
#include <cstdio>
#include <cstring>
#include <cstdlib>
#define PLAYOUT_STATS
#include "../game.cpp"
#include "../ai.cpp"
uint8_t Arduboy2Base::sBuffer[1024];
static Game game; static AI ai;
// game_002.sgf moves (from evalprobe)
static const char *SGF =
 "B[ed]W[dc]B[dd]W[cc]B[ef]W[fb]B[df]W[gd]B[ge]W[he]B[gf]W[hf]B[gg]W[be]"
 "B[ec]W[cg]B[gh]W[fd]B[fc]W[gc]B[db]W[eb]B[cb]W[fe]B[bf]W[cf]B[bg]W[cd]"
 "B[bc]W[bd]B[bb]W[bh]B[hd]W[eh]B[ff]W[ih]B[gb]W[hb]B[hc]W[ga]";
static void runPhase(const char *name, int nmoves){
  game.reset();
  const char *p=SGF; uint8_t color=BLACK;
  for(int i=0;i<nmoves;i++){
    p=strchr(p,'[')+1;
    uint8_t x=p[0]-'a', y=p[1]-'a';
    game.set(x,y,color); color=3-color; p+=2;
  }
  game.turn=color;
  plN=plMoves=plEndCap=plEndPass=plEndMercy=0;
  simKomi=game.kpieces; rootTurn=game.turn;
  scoreMode=0;
  loadRootBoard(game); // builds the no-go masks
  rootStones=0;
  for(uint8_t i=0;i<81;i++){ if(simBoard[i]!=EMPTY) rootStones++; }
  rngState=12345;
  for(int k=0;k<2000;k++){
    for(uint8_t i=0;i<81;i++) simBoard[i]=packedGet(game.board,i);
    playout(game.turn,0xFF,0xFF);
  }
  printf("%-22s stones=%2u  avg len %5.1f  endings: pass %4.1f%%  mercy %4.1f%%  cap %4.1f%%\n",
    name, rootStones, (double)plMoves/plN,
    100.0*plEndPass/plN, 100.0*plEndMercy/plN, 100.0*plEndCap/plN);
}
static const char *LOST[9]={
 "X.XO...X.","XXXXX.XOO","XOXOXOXO.","XOOOX.XOO","O..OX.XXX","OO.OXXXOO",".OOOOOXO.","....OOXOX",".....OXO."};
int main(){
  runPhase("opening (8 stones)",8);
  runPhase("midgame (24 stones)",24);
  runPhase("late (40 stones)",40);
  // endgame board with a fillable region
  game.reset();
  for(uint8_t y=0;y<9;y++)for(uint8_t x=0;x<9;x++){char c=LOST[y][x];game.set(x,y,c=='X'?BLACK:c=='O'?WHITE:EMPTY);}
  game.turn=WHITE;
  plN=plMoves=plEndCap=plEndPass=plEndMercy=0;
  simKomi=game.kpieces; rootTurn=game.turn; scoreMode=0;
  loadRootBoard(game);
  uint8_t rootSim[81]; memcpy(rootSim,simBoard,81);
  rootStones=0; for(uint8_t i=0;i<81;i++) if(rootSim[i]!=EMPTY) rootStones++;
  rngState=12345;
  for(int k=0;k<2000;k++){ memcpy(simBoard,rootSim,81); playout(game.turn,0xFF,0xFF); }
  printf("%-22s stones=%2u  avg len %5.1f  endings: pass %4.1f%%  mercy %4.1f%%  cap %4.1f%%\n",
    "endgame (63 stones)", rootStones, (double)plMoves/plN,
    100.0*plEndPass/plN, 100.0*plEndMercy/plN, 100.0*plEndCap/plN);
  return 0;
}
