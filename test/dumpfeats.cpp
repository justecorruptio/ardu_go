#include <cstdio>
#include <cstdlib>
#include "../game.cpp"
#include "../ai.cpp"
uint8_t Arduboy2Base::sBuffer[1024];
int main(int argc, char** argv){
    FILE* f=fopen(argv[1],"r"); if(!f){fprintf(stderr,"no %s\n",argv[1]);return 1;}
    static char line[8192]; Game game; AI ai;
    while(fgets(line,sizeof line,f)){
        char* p=line;
        long idx=strtol(p,&p,10), tm=strtol(p,&p,10), lx=strtol(p,&p,10), ly=strtol(p,&p,10), ns=strtol(p,&p,10);
        game.reset(); ai.reset();
        for(long i=0;i<ns;i++){ long x=strtol(p,&p,10),y=strtol(p,&p,10),c=strtol(p,&p,10); game.set((uint8_t)x,(uint8_t)y,(uint8_t)c); }
        game.turn=(uint8_t)tm; ai.notifyMove((uint8_t)lx,(uint8_t)ly);
        printf("POS %ld\n", idx);
        uint8_t before[PACKED_BOARD_BYTES]; memcpy(before, game.board, sizeof before);
        ai.chooseMove(game);
        int mv=-1;
        for(uint8_t i=0;i<BOARD_CELLS;i++)
            if(packedGet(game.board,i)!=EMPTY && packedGet(before,i)==EMPTY) mv=i;
        printf("MOVE %ld %d\n", idx, mv);
    }
    return 0;
}
