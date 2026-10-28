#include "../src/sim.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
int main(void){
 Game g;rh_new_game(&g);g.traffic_count=2;rh_set_player(&g,100,100,0);
 g.cars[0]=(Traffic){477.2f,420,0,20,2,1,3};g.cars[1]=(Traffic){420,482.8f,RH_PI/2,20,3,1,3};
 for(int i=0;i<1200;i++)rh_step(&g,(Controls){0},1.f/60);
 assert(g.cars[0].z>510&&g.cars[1].x>500);
 rh_new_game(&g);g.traffic_count=1;g.transport_mode=3;rh_set_player(&g,-2.8f,0,0);
 g.cars[0]=(Traffic){-2.8f,-5,0,0,0,1,0};g.player.v=1;
 rh_step(&g,(Controls){.throttle=1},.016f);assert(g.player.z>0);
 rh_new_game(&g);g.traffic_count=1;g.job.stage=1;rh_set_player(&g,-2.8f,0,0);
 rh_step(&g,(Controls){.menu_left=1},.016f);assert(g.parked_valid);rh_set_player(&g,50,50,0);
 g.cars[0]=(Traffic){-2.8f,-60,0,20,0,1,0};
 for(int i=0;i<600;i++)rh_step(&g,(Controls){0},1.f/60);
 assert(g.cars[0].z< -10&&fabsf(g.cars[0].speed)<.1f);
 rh_new_game(&g);g.traffic_count=0;g.job.stage=1;rh_set_player(&g,-2.8f,0,0);
 rh_step(&g,(Controls){.menu_left=1},.016f);rh_set_player(&g,-2.8f,-35,0);
 for(int i=0;i<500;i++)rh_step(&g,(Controls){.throttle=1},1.f/60);
 assert(g.player.z< -12);
 puts("city safety: OK");
}
