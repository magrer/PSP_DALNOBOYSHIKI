#include "../src/sim.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
static void advance(Game *g,Controls c,int n){for(int i=0;i<n;i++)rh_step(g,c,1.0f/60);}
static void isolated(Game *g){rh_new_game(g);g->traffic_count=0;g->weather_mode=1;rh_set_player(g,2.8f,0,0);}
static void constraint(const Player *p){
 float hx=p->x-sinf(p->yaw)*RH_TRAILER_HITCH,hz=p->z-cosf(p->yaw)*RH_TRAILER_HITCH;
 float cx=p->trailer_x+sinf(p->trailer_yaw)*RH_TRAILER_CENTER;
 float cz=p->trailer_z+cosf(p->trailer_yaw)*RH_TRAILER_CENTER;
 assert(rh_dist2(hx,hz,cx,cz)<.002f);
 assert(isfinite(p->x)&&isfinite(p->z)&&isfinite(p->trailer_yaw));
}
typedef struct {float x,z,yaw,v,speed_abs,rpm,fuel,odo,damage,trailer_yaw,engine_t,gearbox_t;int gear,model,camera,reverse;} OldPlayer;
typedef struct {OldPlayer player;Job job;Traffic cars[RH_MAX_TRAFFIC];int traffic_count,paused,map_open,completed,last_notice,notice_ticks;unsigned int owned_trucks;float bank,drive_time,traffic_t,sim_t,clock_hours;uint32_t rng;} OldGame;
static void migration(void){
 Game g,out;isolated(&g);OldGame old;memset(&old,0,sizeof old);
 memcpy(&old.player,&g.player,sizeof old.player);old.job=g.job;old.owned_trucks=1;old.bank=12345;old.clock_hours=8;old.rng=42;
 uint32_t h=2166136261u;const unsigned char *b=(const unsigned char*)&old;
 for(size_t i=0;i<sizeof old;i++){h^=b[i];h*=16777619u;}
 uint32_t header[3]={0x52485356u,5,h};FILE *f=fopen("/tmp/rh_v5.sav","wb");assert(f);
 assert(fwrite(header,1,sizeof header,f)==sizeof header);assert(fwrite(&old,1,sizeof old,f)==sizeof old);fclose(f);
 assert(rh_load(&out,"/tmp/rh_v5.sav"));assert(out.bank==12345&&out.weather_mode==0&&out.time_mode==0&&out.player.fuel_pending==0&&out.service_phase==0&&out.service_timer==0);
 constraint(&out.player);
}
static void check_lane(const Traffic *t){
 int gx=(int)floorf(t->x/RH_ROAD_SPACING+.5f),gz=(int)floorf(t->z/RH_ROAD_SPACING+.5f);
 float offset=t->axis>=2 ? ((t->axis&1)?t->z-RH_CITY_BLOCK*floorf(t->z/RH_CITY_BLOCK+.5f):t->x-RH_CITY_BLOCK*floorf(t->x/RH_CITY_BLOCK+.5f)) : (t->axis==0?t->x-rh_road_x(gx,t->z):t->z-rh_road_z(gz,t->x));
 float expected=((t->axis&1)?1:-1)*t->direction*2.8f;
 assert(fabsf(offset-expected)<.01f);
}
static void traffic_lanes(void){
 Game g;rh_new_game(&g);
 assert(fabsf(g.player.x-rh_road_x(0,g.player.z)+2.8f)<.01f);
 for(int i=0;i<g.traffic_count;i++)check_lane(&g.cars[i]);
 g.traffic_count=4;
 for(int i=0;i<4;i++){
  Traffic *t=&g.cars[i];t->axis=i/2;t->direction=i%2?1:-1;t->speed=t->direction*10;t->variant=0;
  if(t->axis==0){t->z=(i+1)*90;t->x=rh_road_x(0,t->z)-t->direction*2.8f;t->yaw=t->direction>0?0:RH_PI;}
  else{t->x=(i+1)*90;t->z=rh_road_z(0,t->x)+t->direction*2.8f;t->yaw=t->direction>0?RH_PI/2:-RH_PI/2;}
 }
 advance(&g,(Controls){0},60);
 for(int i=0;i<4;i++){assert(g.cars[i].axis==i/2);assert(g.cars[i].direction==(i%2?1:-1));check_lane(&g.cars[i]);}
}
static void cruise_fuel(void){
 Game g;isolated(&g);rh_set_player(&g,rh_road_x(0,0)-2.8f,0,0);
 float initial=g.player.fuel;
 for(int i=0;i<60000&&g.player.odo<5;i++){
  float look=22+g.player.speed_abs*2,tx=rh_road_x(0,g.player.z+look)-2.8f;
  float error=atan2f(tx-g.player.x,look)-g.player.yaw;
  while(error>RH_PI)error-=2*RH_PI;
  while(error< -RH_PI)error+=2*RH_PI;
  Controls c={0};c.steer=fmaxf(-1,fminf(1,error*1.8f));
  c.throttle=fmaxf(0,fminf(1,(12-g.player.v)*.45f));
  c.brake=fmaxf(0,fminf(1,(g.player.v-13)*.4f));rh_step(&g,c,1.0f/60);
 }
 assert(g.player.odo>=5&&g.player.z>4900);
 float used=initial-g.player.fuel;
 assert(used>4&&used<8); /* plausible nonzero consumption, not a calibration claim */
 assert(g.player.fuel_pending>=0&&g.player.fuel_pending<.0011f);
 assert(rh_save(&g,"/tmp/rh_fuel.sav"));Game loaded;assert(rh_load(&loaded,"/tmp/rh_fuel.sav"));
 assert(loaded.player.fuel_pending==g.player.fuel_pending);
 g.player.fuel=.0001f;g.player.fuel_pending=.00099f;
 advance(&g,(Controls){0},10);assert(g.player.fuel==0&&g.player.fuel_pending==0);
 printf("Cruise fuel over 5km: %.3f L\n",used);
}
/* Independent projection SAT, used after every travel step. */
static int rectangles_overlap(float x,float z,float yaw,float hw,float hl,float ox,float oz,float oyaw,float ow,float ol){
 float a[2][2]={{cosf(yaw),-sinf(yaw)},{sinf(yaw),cosf(yaw)}};
 float b[2][2]={{cosf(oyaw),-sinf(oyaw)},{sinf(oyaw),cosf(oyaw)}};
 for(int i=0;i<4;i++){
  float *axis=i<2?a[i]:b[i-2];
  float ar=hw*fabsf(axis[0]*a[0][0]+axis[1]*a[0][1])+hl*fabsf(axis[0]*a[1][0]+axis[1]*a[1][1]);
  float br=ow*fabsf(axis[0]*b[0][0]+axis[1]*b[0][1])+ol*fabsf(axis[0]*b[1][0]+axis[1]*b[1][1]);
  if(fabsf((ox-x)*axis[0]+(oz-z)*axis[1])>=ar+br-.0001f)return 0;
 }
 return 1;
}
static void separated(const Game *g){
 const Player *p=&g->player;const Traffic *t=&g->cars[0];float hw=t->variant==3?1.25f:.975f,hl=t->variant==3?4.3f:2.3f;
 assert(!rectangles_overlap(p->x,p->z,p->yaw,1.25f,4.3f,t->x,t->z,t->yaw,hw,hl));
 if(g->job.stage==1)assert(!rectangles_overlap(p->trailer_x,p->trailer_z,p->trailer_yaw,1.3f,4.3f,t->x,t->z,t->yaw,hw,hl));
}
static void traffic_obstacles(void){
 for(int scenario=0;scenario<3;scenario++){
  Game g;isolated(&g);g.traffic_count=1;
  rh_set_player(&g,-2.8f,scenario==2?8:0,0);g.job.stage=scenario?1:0;
  Traffic *t=&g.cars[0];t->variant=scenario==1?3:0;t->direction=1;t->axis=scenario==2?1:0;t->speed=22;
  if(t->axis==0){t->z=-60;t->x=rh_road_x(0,t->z)-2.8f;t->yaw=0;}
  else{t->x=-60;t->z=rh_road_z(0,t->x)+2.8f;t->yaw=RH_PI/2;}
  for(int i=0;i<1200;i++){Controls c={0};c.handbrake=1;rh_step(&g,c,1.0f/60);separated(&g);constraint(&g.player);}
  assert(fabsf(t->speed)<.1f);
  if(scenario==0)assert(t->z< -5.55f);
  if(scenario==1)assert(t->z< -14.3f);
  if(scenario==2)assert(t->x< -6.15f);
 }
 /* The player cannot drive through an approaching NPC either. */
 Game g;isolated(&g);rh_set_player(&g,2.8f,-25,0);g.player.v=20;g.traffic_count=1;
 Traffic *t=&g.cars[0];t->x=2.8f;t->z=0;t->axis=0;t->direction=-1;t->yaw=RH_PI;t->speed=-15;t->variant=3;
 for(int i=0;i<180;i++){rh_step(&g,(Controls){0},1.0f/60);separated(&g);}
 assert(g.player.speed_abs<.1f);
}
static void traffic_crossing(void){
 Game g;isolated(&g);rh_set_player(&g,100,100,0);g.traffic_count=2;
 Traffic *a=&g.cars[0],*b=&g.cars[1];
 *a=(Traffic){-2.8f,-4,0,15,0,1,0};*b=(Traffic){-6.4f,2.8f,RH_PI/2,15,1,1,0};
 for(int i=0;i<600;i++){
  rh_step(&g,(Controls){0},1.0f/60);
  assert(!rectangles_overlap(a->x,a->z,a->yaw,.975f,2.3f,b->x,b->z,b->yaw,.975f,2.3f));
 }
 assert(a->z>30&&b->x>20); /* both clear the junction, not merely stop safely */
 /* A normal higher-speed approach waits at the stop line, then resumes. */
 *a=(Traffic){-2.8f,-60,0,20,0,1,3};*b=(Traffic){-60,2.8f,RH_PI/2,20,1,1,3};
 for(int i=0;i<1200;i++){
  rh_step(&g,(Controls){0},1.0f/60);
  assert(!rectangles_overlap(a->x,a->z,a->yaw,1.25f,4.3f,b->x,b->z,b->yaw,1.25f,4.3f));
 }
 assert(a->z>30&&b->x>20);
 /* Opposite lanes remain independently traversable. */
 *a=(Traffic){-2.8f,-30,0,15,0,1,3};*b=(Traffic){2.8f,30,RH_PI,-15,0,-1,3};
 for(int i=0;i<240;i++){
  rh_step(&g,(Controls){0},1.0f/60);
  assert(!rectangles_overlap(a->x,a->z,a->yaw,1.25f,4.3f,b->x,b->z,b->yaw,1.25f,4.3f));
 }
 assert(a->z>10&&b->z< -10);
 /* At z=-6 a passenger nose would enter the rendered rear chassis.
    Starting outside it, a fast close approach must stop before z=-6.35. */
 isolated(&g);g.traffic_count=1;rh_set_player(&g,-2.8f,0,0);
 a=&g.cars[0];*a=(Traffic){-2.8f,-7,0,24,0,1,0};
 for(int i=0;i<120;i++){rh_step(&g,(Controls){0},1.0f/60);separated(&g);}
 assert(g.player.damage>0&&g.player.odo>0); /* rear impact now transfers momentum */
}
static void rescue_service(void){
 Game g;isolated(&g);g.map_open=1;g.player.fuel=0;
 Controls c={0};c.action=1;rh_step(&g,c,0);
 assert(g.service_phase==1&&g.service_timer==0&&g.player.fuel==0);
 float startx=g.service_x,startz=g.service_z;
 rh_step(&g,c,.05f);float timer=g.service_timer;
 rh_step(&g,c,0);assert(g.service_timer==timer); /* duplicate must not reset timer */
 for(int i=1;i<999;i++)rh_step(&g,(Controls){0},.05f);
 assert(g.service_phase==1&&g.player.fuel==0&&g.service_timer>49.9f);
 rh_step(&g,(Controls){0},.05f);assert(g.service_phase==2&&g.player.fuel==0);
 assert(g.clock_hours>8.16f&&g.sim_t>49.9f); /* map overlay keeps the world clock running */
 assert(fabsf(rh_dist2(startx,startz,g.service_x,g.service_z)-200)<.02f);
 assert(fabsf(g.service_x-g.player.x+4.5f)<.01f);
 float x=g.player.x,z=g.player.z,clock=g.clock_hours;
 g.map_open=0;c=(Controls){0};c.throttle=1;c.steer=1;
 for(int i=0;i<599;i++)rh_step(&g,c,.05f);
 assert(g.service_phase==2&&g.player.fuel==0&&g.player.x==x&&g.player.z==z);
 assert(g.clock_hours>clock&&g.sim_t>29); /* world continues during refilling */
 assert(rh_save(&g,"/private/tmp/rh_service.sav"));Game saved;assert(rh_load(&saved,"/private/tmp/rh_service.sav"));
 assert(saved.service_phase==2&&saved.service_timer==g.service_timer&&saved.service_x==g.service_x&&saved.service_z==g.service_z&&saved.service_yaw==g.service_yaw);
 g.map_open=1;g.player.fuel_pending=.0009f;rh_step(&g,(Controls){0},.05f);assert(g.service_phase==3&&g.player.fuel==80&&g.player.fuel_pending<.0005f);
 float parkedx=g.service_x,parkedz=g.service_z;
 for(int i=0;i<159;i++)rh_step(&g,(Controls){0},.05f);assert(g.service_phase==3);
 rh_step(&g,(Controls){0},.05f);assert(g.service_phase==0);
 assert(fabsf(rh_dist2(parkedx,parkedz,g.service_x,g.service_z)-120)<.02f);
 c=(Controls){0};c.action=1;rh_step(&g,c,0);assert(g.service_phase==0); /* fuel is available */
 g.player.fuel=0;rh_step(&g,c,0);assert(g.service_phase==1); /* future breakdown can call again */
 g.paused=1;timer=g.service_timer;rh_step(&g,(Controls){0},.05f);assert(g.service_timer==timer);
 isolated(&g);g.player.fuel=0;g.map_open=1;g.player.speed_abs=1;rh_step(&g,c,0);assert(g.service_phase==0);
 isolated(&g);g.clock_hours=0;for(int i=0;i<100;i++)rh_step(&g,(Controls){0},.05f);
 assert(fabsf(g.clock_hours-1.0f/60)<.00001f); /* 5 real seconds = 1 game minute */
}
static void map_reverse_brake(void){
 Game g;rh_new_game(&g);g.traffic_count=0;rh_set_player(&g,-2.8f,0,0);
 g.player.reverse=1;g.map_open=1;
 for(int i=0;i<600;i++)rh_step(&g,(Controls){0},1.0f/60);
 assert(g.player.speed_abs<.01f&&g.player.odo<.0001f);
}
static void city_grid(void){
 int count=0;
 for(int tz=-15;tz<15;tz++)for(int tx=-15;tx<15;tx++)count+=rh_city_tile(tx,tz);
 assert(count==100);
 assert(rh_city_tile(-5,-5)&&rh_city_tile(4,4)&&!rh_city_tile(5,4)&&!rh_city_tile(-6,0));
 assert(rh_city_tile(25,-5)&&rh_city_tile(34,4)&&!rh_city_tile(35,4));
 assert(!rh_city_tile(15,0)&&!rh_city_tile(0,15));
 for(int i=-5;i<=5;i++){
  assert(rh_cityroad_distance(i*480,1300)==0);
  assert(rh_road_distance(1300,i*480)==0);
 }
 assert(rh_cityroad_distance(2405,1300)<=5.01f);
 assert(rh_cityroad_distance(2410,1300)>1000);
 assert(rh_cityroad_distance(7200,7200)>1000);
 /* Local streets support actual travel through all ten city blocks. */
 Game g;isolated(&g);rh_set_player(&g,480-2.8f,-2300,0);
 g.player.v=12;Controls c={0};c.throttle=.4f;
 for(int i=0;i<30000&&g.player.z<2300;i++){
  rh_step(&g,c,1.0f/60);
  assert(rh_cityroad_distance(g.player.x,g.player.z)<8);
 }
 assert(g.player.z>=2300&&g.player.damage==0);
}
int main(void){map_reverse_brake();
 migration();traffic_lanes();cruise_fuel();traffic_obstacles();traffic_crossing();rescue_service();city_grid();
 for(int x=-4;x<=4;x++)for(int z=-4;z<=4;z++){
  assert(fabsf(rh_road_x(x,z*RH_ROAD_SPACING)-x*RH_ROAD_SPACING)<.01f);
  assert(fabsf(rh_road_z(z,x*RH_ROAD_SPACING)-z*RH_ROAD_SPACING)<.01f);
  float px=x*RH_ROAD_SPACING+700,pz=rh_road_z(z,px);
  assert(rh_road_distance(px,pz)<.01f);
  rh_depot(x,z,&px,&pz);assert(rh_road_distance(px,pz)<.01f);
 }
 assert(rh_settlement_kind(0,0)==2&&rh_settlement_kind(8,0)==2);
 Game g;isolated(&g);g.job.stage=1;g.player.v=8;g.player.trailer_yaw=.1f;
 Controls c={0};c.steer=.4f;c.throttle=.5f;
 for(int i=0;i<1800;i++){rh_step(&g,c,1.0f/60);constraint(&g.player);}
 isolated(&g);g.job.stage=1;g.player.reverse=1;g.player.trailer_yaw=.1f;
 c=(Controls){0};c.brake=1;advance(&g,c,120);
 assert(g.player.v<0&&fabsf(g.player.yaw-g.player.trailer_yaw)>.1f);constraint(&g.player);
 Game dry,ice;isolated(&dry);ice=dry;dry.player.v=ice.player.v=15;ice.weather_mode=4;ice.weather=3;
 c=(Controls){0};c.brake=1;advance(&dry,c,120);advance(&ice,c,120);
 assert(ice.player.speed_abs>dry.player.speed_abs+1);
 isolated(&g);float threshold=g.weather_next_odo;assert(threshold>=3&&threshold<=10);
 g.player.odo=threshold;int weather=g.weather;rh_step(&g,(Controls){0},.016f);
 assert(g.weather==weather); /* explicit clear mode does not autoswitch */
 g.weather_mode=0;rh_step(&g,(Controls){0},.016f);
 assert(g.weather!=weather&&g.weather_next_odo-g.player.odo>=3&&g.weather_next_odo-g.player.odo<=10);
 /* Existing saves may retain the former 50–150 km schedule. */
 g.weather_next_odo=g.player.odo+100;weather=g.weather;rh_step(&g,(Controls){0},.016f);
 assert(g.weather==weather&&g.weather_next_odo-g.player.odo>=3&&g.weather_next_odo-g.player.odo<=10);
 for(int n=0;n<40;n++){g.player.odo=g.weather_next_odo;weather=g.weather;rh_step(&g,(Controls){0},.016f);
  assert(g.weather!=weather&&g.weather_next_odo-g.player.odo>=3&&g.weather_next_odo-g.player.odo<=10);}
 c=(Controls){0};c.pause=1;rh_step(&g,c,.016f);float clock=g.clock_hours,odo=g.player.odo;
 c=(Controls){0};c.menu_right=1;rh_step(&g,c,.016f);assert(g.weather_mode==1);
 c=(Controls){0};c.menu_down=1;c.menu_right=1;rh_step(&g,c,.016f);
 assert(g.settings_row==1&&g.time_mode==1&&g.clock_hours==12&&g.player.odo==odo);
 c=(Controls){0};c.menu_right=1;rh_step(&g,c,.016f);assert(g.time_mode==2&&g.clock_hours==17.6f);
 rh_step(&g,c,.016f);rh_step(&g,c,.016f);assert(g.time_mode==4&&g.clock_hours==6.4f);
 c.menu_left=1;c.menu_right=0;rh_step(&g,c,.016f);rh_step(&g,c,.016f);rh_step(&g,c,.016f);assert(g.time_mode==1);
 c=(Controls){0};c.pause=1;rh_step(&g,c,.016f);advance(&g,(Controls){0},120);assert(g.clock_hours==12);(void)clock;
 /* Warehouse southern wall is z=157 at depot (0,0). A tractor
    facing south clears it, but its attached trailer hits while reversing. */
 Game solo,tow;isolated(&solo);rh_set_player(&solo,117,146,RH_PI);solo.player.reverse=1;solo.player.v=-4;
 tow=solo;tow.job.stage=1;
 c=(Controls){0};c.brake=1;advance(&solo,c,20);advance(&tow,c,20);
 assert(solo.player.z>146.5f&&tow.player.z<146.25f&&tow.player.damage>0);
 constraint(&tow.player);
 /* City tile (0,0), k=0 centered (141,141), west wall x=129. */
 isolated(&g);rh_set_player(&g,123,141,RH_PI/2);g.player.v=10;
 advance(&g,(Controls){0},120);assert(g.player.x <125&&g.player.damage>0);
 /* Loading bay remains usable with the full trailer footprint. */
 isolated(&g);float loadingx,loadingz;rh_depot(0,0,&loadingx,&loadingz);rh_set_player(&g,loadingx,loadingz,0);
 c=(Controls){0};c.action=1;rh_step(&g,c,.016f);assert(g.job.stage==1);
 assert(g.player.damage==0);constraint(&g.player);
 /* A river crossing is traversable on its bridge but banks block entry. */
 isolated(&g);rh_set_player(&g,680,300,RH_PI/2);g.player.v=20;advance(&g,(Controls){0},300);
 assert(!rh_water(g.player.x,g.player.z)||rh_road_distance(g.player.x,g.player.z)<=10);
 assert(rh_save(&g,"/tmp/rh_features.sav"));Game saved;assert(rh_load(&saved,"/tmp/rh_features.sav"));
 assert(saved.weather_mode==g.weather_mode&&saved.player.trailer_x==g.player.trailer_x);
 puts("PASS: curved roads, rigid hitch, reverse articulation, ice braking, pause settings, weather cadence, water barrier, save v6");return 0;
}
