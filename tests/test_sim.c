#include "../src/sim.h"
#include <math.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
static void drive(Game *g,float gas,float brake,float steer,int frames){Controls c={0};c.throttle=gas;c.brake=brake;c.steer=steer;for(int i=0;i<frames;i++)rh_step(g,c,1.0f/60.0f);}
int main(void){
 Game g;rh_new_game(&g);
 assert(g.job.stage==0 && g.job.distance_km>=9.0f);
 assert(g.player.fuel==RH_TRUCKS[0].tank_l);
 float x,z;rh_depot(0,0,&x,&z);
 assert(rh_road_distance(x,z)<7);
 assert(fabsf(rh_height(0,0)-rh_height(.1f,0))<.1f);
 drive(&g,1,0,0,1200);printf("Loaded-neutral speed (km/h): %.1f\n",g.player.v*3.6f);
 assert(g.player.v>10 && g.player.v<43);
 float initial=g.player.v;drive(&g,0,1,0,420);
 printf("Brake %0.1f -> %0.1f km/h\n",initial*3.6f,g.player.v*3.6f);
 assert(g.player.v>=0 && g.player.v<1);
 Controls reverse={0};reverse.brake=1;reverse.reverse_request=1;
 rh_step(&g,reverse,1.0f/60.0f);reverse.reverse_request=0;
 for(int i=0;i<150;i++)rh_step(&g,reverse,1.0f/60.0f);
 assert(g.player.v<-.2f);
 rh_set_player(&g,x,z,0);
 rh_set_player(&g,x,z,0);Controls a={0};a.action=1;rh_step(&g,a,.016f);
 assert(g.job.stage==1 && g.job.cargo_tons>0);
 float wx,wz;rh_waypoint(&g,&wx,&wz);
 assert(isfinite(wx)&&isfinite(wz));
 int dx=g.job.dest_x,dz=g.job.dest_z;
 float payout=g.job.payout,balance=g.bank;
 rh_depot(dx,dz,&x,&z);rh_set_player(&g,x,z,0);rh_step(&g,a,.016f);
 assert(g.job.stage==0 && g.completed==1 && fabsf(g.bank-balance-payout)<.1f);
 assert(rh_save(&g,"test_state.dat"));Game loaded;assert(rh_load(&loaded,"test_state.dat"));
 assert(loaded.bank==g.bank && loaded.completed==1);
 FILE *f=fopen("test_state.dat","r+b");assert(f);fseek(f,50,SEEK_SET);fputc(0xFF,f);fclose(f);
 assert(!rh_load(&loaded,"test_state.dat"));remove("test_state.dat");
 Game t;rh_new_game(&t);rh_set_player(&t,0,100,0);drive(&t,1,0,0,1000);
 assert(t.player.odo>.001 && t.player.fuel<RH_TRUCKS[0].tank_l);
 printf("PASS: terrain, generated jobs, throttle, brake, cargo, economy, save, corruption check\n");
 return 0;
}
