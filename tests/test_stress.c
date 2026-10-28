#include "../src/sim.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void valid(const Game *g){
 const Player *p=&g->player;
 assert(isfinite(p->x)&&isfinite(p->z)&&isfinite(p->v)&&isfinite(p->rpm));
 assert(isfinite(p->yaw)&&isfinite(p->fuel)&&isfinite(p->trailer_yaw));
 assert(isfinite(g->bank)&&isfinite(g->sim_t)&&isfinite(g->clock_hours));
 assert(p->model>=0&&p->model<3&&p->gear>=1&&p->gear<=8);
 assert(p->fuel>=0&&p->fuel<=RH_TRUCKS[p->model].tank_l+0.01f);
 assert(p->speed_abs<=25.001f&&p->damage<=100.01f&&p->damage>=0);
 assert(g->bank>=0);
 for(int j=0;j<g->traffic_count;j++){
  assert(isfinite(g->cars[j].x)&&isfinite(g->cars[j].z));
  assert(isfinite(g->cars[j].yaw)&&isfinite(g->cars[j].speed));
 }
}
static float clamp(float v,float lo,float hi){return v<lo?lo:v>hi?hi:v;}
static Controls road_driver(const Game *g){
 const Player *p=&g->player;Controls c={0};
 /* Follow a real curved northbound lane, looking ahead instead of applying
    arbitrary steering which inevitably drives into terrain obstacles. */
 float look=22+p->speed_abs*2,tx=rh_road_x(0,p->z+look)-2.8f;
 float heading=atan2f(tx-p->x,look),error=heading-p->yaw;
 while(error>RH_PI)error-=2*RH_PI;
 while(error< -RH_PI)error+=2*RH_PI;
 c.steer=clamp(error*1.8f,-1,1);
 float target=g->weather==3?7:g->weather==2?10:14;
 c.throttle=clamp((target-p->v)*.45f,0,1);
 c.brake=clamp((p->v-target-1)*.4f,0,1);
 return c;
}
int main(void){
 Game a,b;rh_new_game(&a);rh_new_game(&b);
 int road_frames=0;
 for(int i=0;i<45000;i++){
  /* Exercise each surface while maintaining the same reproducible route. */
  a.weather_mode=b.weather_mode=1+(i/9000)%4;
  a.weather=b.weather=a.weather_mode-1;
  Controls c=road_driver(&a);
  rh_step(&a,c,1.0f/60.0f);
  rh_step(&b,c,1.0f/60.0f);
  if(rh_road_distance(a.player.x,a.player.z)<8)road_frames++;
  if((i&63)==0)valid(&a);
 }
 assert(road_frames>44500);
 assert(a.player.z>3000); /* actual displacement, not only accumulated wheel distance */
 valid(&a);
 assert(memcmp(&a,&b,sizeof a)==0);
 assert(rh_save(&a,"stress.dat"));
 assert(rh_save(&a,"stress.dat"));
 Game other;assert(rh_load(&other,"stress.dat"));
 assert(memcmp(&a,&other,sizeof a)==0);
 assert(rh_load(&other,"stress.dat.BAK"));
 FILE *f=fopen("stress.dat","r+b");assert(f);fseek(f,3,SEEK_SET);fputc(222,f);fclose(f);
 assert(!rh_load(&other,"stress.dat"));
 assert(rh_load(&other,"stress.dat.BAK"));
 remove("stress.dat");remove("stress.dat.BAK");
 printf("ODO %.3f, fuel %.3f, speed %.3f, gear %d\n",a.player.odo,a.player.fuel,a.player.v,a.player.gear); assert(a.player.odo>3.0f);
 printf("PASS stress: 45k frames, deterministic simulation, fuel, finite positions, rollback save; %.1f km driven\n",a.player.odo);
}
