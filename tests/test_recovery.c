#include "../src/sim.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
static void step(Game *g,Controls c,int count){for(int i=0;i<count;i++)rh_step(g,c,1.f/60);}
static void recovery(void){
 Game g;rh_new_game(&g);g.traffic_count=0;g.job.stage=1;
 float x=0,z=0;int found=0;
 for(int i=0;i<100&&!found;i++)for(int j=0;j<100&&!found;j++)if(rh_tree_at(i,j,&x,&z))found=1;
 assert(found);rh_set_player(&g,x,z,0);float fuel=g.player.fuel;Job job=g.job;
 step(&g,(Controls){0},600);assert(rh_dist2(x,z,g.player.x,g.player.z)<.1f);
 step(&g,(Controls){.throttle=1,.brake=1},600);assert(rh_dist2(x,z,g.player.x,g.player.z)<.1f);
 step(&g,(Controls){.throttle=1},600);assert(rh_dist2(x,z,g.player.x,g.player.z)>10);
 assert(g.job.stage==job.stage&&g.job.dest_x==job.dest_x&&g.player.fuel<=fuel&&g.player.fuel>0);
 float hx=g.player.x-sinf(g.player.yaw)*RH_TRAILER_HITCH,hz=g.player.z-cosf(g.player.yaw)*RH_TRAILER_HITCH;
 assert(rh_dist2(hx,hz,g.player.trailer_x+sinf(g.player.trailer_yaw)*RH_TRAILER_CENTER,g.player.trailer_z+cosf(g.player.trailer_yaw)*RH_TRAILER_CENTER)<.01f);
}
static void push(void){
 /* One contact step distinguishes positional pushing from AI acceleration. */
 Game probe;rh_new_game(&probe);probe.traffic_count=1;probe.job.stage=0;
 rh_set_player(&probe,-2.8f,0,0);probe.player.v=5;probe.player.speed_abs=5;
 probe.cars[0]=(Traffic){.x=-2.8f,.z=6.62f,.yaw=0,.axis=0,.direction=1,.variant=0};
 rh_step(&probe,(Controls){.throttle=1},1.f/60);
 assert(probe.player.z>.02f && probe.cars[0].z>6.70f);
 assert(probe.cars[0].z-probe.player.z>6.4f);
 Game g;rh_new_game(&g);g.traffic_count=1;g.job.stage=0;rh_set_player(&g,-2.8f,0,0);
 Traffic *t=&g.cars[0];*t=(Traffic){.x=-2.8f,.z=7,.yaw=0,.axis=0,.direction=1,.variant=0};
 float start=t->z;int contact=0,pushed=0;
 for(int i=0;i<300;i++){
  /* Reset the car speed: AI acceleration alone moves it less than 0.001 m per step. */
  t->speed=0;float previous=t->z, player_previous=g.player.z, gap=t->z-g.player.z;
  rh_step(&g,(Controls){.throttle=1},1.f/60);
  if(t->z-g.player.z<8)contact=1;
  if(gap<6.75f && t->z-previous>.01f && g.player.z-player_previous>.005f)pushed=1;
  /* The player must stay behind the car throughout the encounter. */
  assert(t->z-g.player.z>6.4f);
 }
 assert(contact&&pushed&&t->z-start>1&&g.player.z>1);
}
int main(void){recovery();push();puts("PASS: recovery, hitch, parked and braking conditions, push without passing through");}
