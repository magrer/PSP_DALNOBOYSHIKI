#include "../src/sim.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
int main(void){
 Game g;rh_new_game(&g);rh_set_player(&g,477.2f,700,0);
 for(int i=0;i<g.traffic_count;i++){g.cars[i].x=10000;g.cars[i].z=10000;}
 rh_step(&g,(Controls){0},.016f);int nearby=0,city=0;
 for(int i=0;i<g.traffic_count;i++){Traffic *t=&g.cars[i];if(rh_dist2(t->x,t->z,g.player.x,g.player.z)<350)nearby++;if(t->axis>=2)city++;}
 assert(nearby>=3&&city>=3);
 for(int n=0;n<120;n++)rh_step(&g,(Controls){0},1.f/60);
 for(int i=0;i<g.traffic_count;i++){Traffic *t=&g.cars[i];if(t->axis>=2)assert(rh_cityroad_distance(t->x,t->z)<5);}
 puts("city traffic: OK");
}
