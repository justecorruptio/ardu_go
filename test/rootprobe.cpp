#include <cstdio>
#include <cstring>
#include "../game.cpp"
#include "../ai.cpp"
uint8_t Arduboy2Base::sBuffer[1024];
static Game game; static AI ai;
static const char *LOST[9]={
 "X.XO...X.","XXXXX.XOO","XOXOXOXO.","XOOOX.XOO","O..OX.XXX","OO.OXXXOO",".OOOOOXO.","....OOXOX",".....OXO."};
int main(){
  game.reset();
  for(uint8_t y=0;y<9;y++)for(uint8_t x=0;x<9;x++){char c=LOST[y][x];game.set(x,y,c=='X'?BLACK:c=='O'?WHITE:EMPTY);}
  game.turn=WHITE;
  ai.reset(); rngState=31337;
  // replicate think()'s root setup then expand only
  rootTurn=game.turn; simKomi=game.kpieces; scoreMode=0;
  loadRootBoard(game);
  rootStones=0; for(uint8_t i=0;i<81;i++) if(simBoard[i]!=EMPTY) rootStones++;
  poolUsed=0; markEpoch=0; memset(simMark,0,sizeof(simMark));
  uint8_t r=newNode(0xFF); (void)r;
  buildChainMap();
  expandNode(0, game.turn, NO_KO, 0xFF);
  printf("root children (a=active, L=latent): ");
  for(uint8_t c=node(0).firstChild;c!=0xFF;c=node(c).nextSibling)
    printf("%c%u ", (node(c).move&0x80)?'L':'a', node(c).move&0x7F);
  printf("\n");
  return 0;
}
