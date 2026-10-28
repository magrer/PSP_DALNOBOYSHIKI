#include "../src/sim.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
static uint32_t sum(const unsigned char *p,size_t n){uint32_t h=2166136261u;while(n--){h^=*p++;h*=16777619u;}return h;}
int main(void){
 Game g;rh_new_game(&g);g.traffic_count=0;
 Controls c={0};c.menu_left=1;rh_step(&g,c,.016f);assert(g.transport_mode==1);
 c=(Controls){0};c.menu_up=1;rh_step(&g,c,.016f);assert(g.on_foot);
 float x=g.person_x,z=g.person_z;c=(Controls){0};c.throttle=1;for(int i=0;i<120;i++)rh_step(&g,c,1.f/60);assert(rh_dist2(x,z,g.person_x,g.person_z)>5);
 c=(Controls){0};c.menu_right=1;rh_step(&g,c,.016f);assert(rh_dist2(g.person_x,g.person_z,g.player.x,g.player.z)<6);
 c=(Controls){0};c.menu_up=1;rh_step(&g,c,.016f);assert(!g.on_foot);
 c=(Controls){0};c.menu_left=1;rh_step(&g,c,.016f);assert(g.transport_mode==2);
 c.menu_left=0;c.throttle=1;for(int i=0;i<1200;i++)rh_step(&g,c,1.f/60);assert(g.player.speed_abs>25);
 g.paused=1;int mode=g.transport_mode;c=(Controls){0};c.menu_left=1;rh_step(&g,c,.016f);assert(g.transport_mode==mode);
 g.paused=0;g.map_open=1;rh_step(&g,c,.016f);assert(g.transport_mode==mode);
 assert(rh_save(&g,"/tmp/rh_transport_save"));Game h;assert(rh_load(&h,"/tmp/rh_transport_save"));assert(h.transport_mode==2);
 /* Exact legacy v7 payload remains readable and defaults to the truck. */
 size_t n=offsetof(Game,transport_mode);uint32_t head[3]={0x52485356u,7,sum((unsigned char*)&g,n)};FILE *f=fopen("/tmp/rh_transport_v7","wb");assert(f);assert(fwrite(head,sizeof head,1,f)==1);assert(fwrite(&g,n,1,f)==1);fclose(f);assert(rh_load(&h,"/tmp/rh_transport_v7"));assert(h.transport_mode==0&&!h.on_foot);
 /* An active load stays with the truck while other vehicles travel. */
 rh_new_game(&g);g.traffic_count=0;g.job.stage=1;float truck_x=g.player.x,truck_z=g.player.z;
 c=(Controls){.menu_left=1};rh_step(&g,c,.016f);assert(g.parked_valid&&g.transport_mode==1);
 assert(rh_save(&g,"/tmp/rh_transport_parked"));Game parked_copy;assert(rh_load(&parked_copy,"/tmp/rh_transport_parked"));assert(parked_copy.parked_valid&&parked_copy.parked_x==truck_x&&parked_copy.parked_z==truck_z&&parked_copy.job.stage==1);remove("/tmp/rh_transport_parked");
 c=(Controls){.throttle=1};float fuel=g.player.fuel;for(int i=0;i<600;i++)rh_step(&g,c,1.f/60);assert(g.player.fuel<fuel);
 g.player.v=g.player.speed_abs=0;c=(Controls){.menu_left=1};rh_step(&g,c,.016f);rh_step(&g,c,.016f);rh_step(&g,c,.016f);assert(g.transport_mode==0&&rh_dist2(truck_x,truck_z,g.player.x,g.player.z)<.01f);
 rh_new_game(&g);g.traffic_count=0;g.transport_mode=3;g.player.fuel=0;z=g.player.z;c=(Controls){.throttle=1};for(int i=0;i<120;i++)rh_step(&g,c,1.f/60);assert(g.player.z>z+2&&g.player.fuel==0);
 /* Vehicle and walker cross both bridge ramps and leave the upper deck. */
 for(int walker=0;walker<2;walker++){
  rh_new_game(&g);g.traffic_count=0;rh_set_player(&g,rh_road_x(0,6620)-2.8f,6620,0);g.transport_mode=walker?0:1;
  if(walker){g.on_foot=1;g.person_x=g.player.x;g.person_z=g.player.z;g.person_yaw=0;}
  c=(Controls){.throttle=1};int seen_upper=0;
  for(int i=0;i<24000;i++){
   float px=walker?g.person_x:g.player.x,pz=walker?g.person_z:g.player.z,yaw=walker?g.person_yaw:g.player.yaw;
   if(pz>7780)break;
   float look=walker?10:20,target=rh_road_x(0,pz+look)-2.8f,angle=atan2f(target-px,look);
   c.steer=fmaxf(-1,fminf(1,(angle-yaw)*4));rh_step(&g,c,1.f/60);
   if(walker?g.person_level:g.player.road_level)seen_upper=1;
  }
  assert(seen_upper);assert((walker?g.person_z:g.player.z)>7780);assert(!(walker?g.person_level:g.player.road_level));
 }
 /* Light cars can refuel and complete their own freight job. */
 for(int mode=1;mode<=2;mode++){
  rh_new_game(&g);g.traffic_count=0;g.transport_mode=mode;float sx,sz;rh_station(0,0,&sx,&sz);rh_set_player(&g,sx,sz,0);g.player.fuel=10;
  c=(Controls){.action=1};rh_step(&g,c,.016f);assert(g.player.fuel>10);
  rh_depot(g.job.pickup_x,g.job.pickup_z,&sx,&sz);rh_set_player(&g,sx,sz,0);rh_step(&g,c,.016f);assert(g.job.stage==1);
  g.player.v=g.player.speed_abs=0; Controls change={.menu_left=1};rh_step(&g,change,.016f);assert(g.transport_mode==mode);
  rh_depot(g.job.dest_x,g.job.dest_z,&sx,&sz);rh_set_player(&g,sx,sz,0);rh_step(&g,c,.016f);assert(g.job.stage==0&&g.completed==1);
  g.player.fuel=0;g.map_open=1;rh_step(&g,c,.016f);assert(g.service_phase==1);
 }
 rh_new_game(&g);g.transport_mode=3;g.player.fuel=0;g.map_open=1;c=(Controls){.action=1};rh_step(&g,c,.016f);assert(g.service_phase==0);
 /* Version 8 payload continues to load. */
 g.transport_mode=3;
 n=offsetof(Game,person_level);head[1]=8;head[2]=sum((unsigned char*)&g,n);f=fopen("/tmp/rh_transport_v7","wb");assert(f);assert(fwrite(head,sizeof head,1,f)==1);assert(fwrite(&g,n,1,f)==1);fclose(f);assert(rh_load(&h,"/tmp/rh_transport_v7"));assert(h.transport_mode==3);
 remove("/tmp/rh_transport_save");remove("/tmp/rh_transport_v7");puts("transport: OK");
}
