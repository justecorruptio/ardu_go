#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "../game.cpp"
#include "../ai.cpp"
uint8_t Arduboy2Base::sBuffer[1024];
static Game game; static AI ai;
static const char *LOST[9]={
 "X.XO...X.","XXXXX.XOO","XOXOXOXO.","XOOOX.XOO","O..OX.XXX","OO.OXXXOO",".OOOOOXO.","....OOXOX",".....OXO."};
int main(){
  for(int s=1;s<=5;s++){
    game.reset();
    for(uint8_t y=0;y<9;y++)for(uint8_t x=0;x<9;x++){char c=LOST[y][x];game.set(x,y,c=='X'?BLACK:c=='O'?WHITE:EMPTY);}
    memcpy(game.prevBoard,game.board,sizeof(game.board)); game.turn=WHITE;
    ai.reset(); srand(s);
    fprintf(stderr,"seed %d thinking...\n",s);
    ai.think(game);
    fprintf(stderr,"  done sims=%u\n",thinkSims);
  }
  return 0;
}
