#include "../src/sim.h"
#include "../src/render_psp.h"
#include <assert.h>
#include "../src/font.h"
#include <stdio.h>
extern int mock_frames,mock_3d,mock_2d,mock_sun_vertices,mock_moon_vertices,mock_star_vertices;
#include "psp_mock/mock_common.h"
extern ScePspFVector3 mock_camera_eye;
int main(void){
 /* Every printable ASCII glyph is non-empty; slash must be a diagonal, not
     the former single broken pixel. All Russian characters share 9 rows. */
 for(int c=33;c<127;c++){
  int pixels=0;for(int r=0;r<9;r++)pixels|=rh_font[c-32][r];assert(pixels);
 }
 assert(rh_font['/'-32][0]!=rh_font['/'-32][6]);
 assert(rh_font['/'-32][0] && rh_font['/'-32][6]);
 for(int c=0;c<66;c++){
  int pixels=0;for(int r=0;r<9;r++)pixels|=rh_font_ru[c][r];assert(pixels);
 }
 Game g;rh_new_game(&g);rh_render_init();
 rh_set_player(&g,0,90,0);g.player.camera=0;g.job.stage=0;
 mock_3d=mock_2d=0;rh_render_frame(&g,0);int tractor_only=mock_3d;
 g.job.stage=1;mock_3d=mock_2d=0;rh_render_frame(&g,0);
 assert(mock_3d>tractor_only);g.job.stage=0;
 const float samples[][2]={{0,0},{40,90},{130,130},{-110,-130},{1500,1800},
   {-1800,-1800},{4500,2700},{92000,-126000},{-500000,500000}};
 for(unsigned i=0;i<sizeof(samples)/sizeof(samples[0]);i++){
  rh_set_player(&g,samples[i][0],samples[i][1],i*.49f);
  g.player.camera=i%2;g.map_open=(i==7);g.notice_ticks=(i==8?90:0);
  mock_3d=mock_2d=0;
  rh_render_frame(&g,(int)(i%3)-1);
  assert(mock_3d>1000 && mock_3d<54001);
  assert(mock_2d>200 && mock_2d<25001);
  printf("frame %u, 3D vertices %d, HUD vertices %d\n",i,mock_3d,mock_2d);
 }
 int max3=0,max2=0;
 for(int i=0;i<128;i++){
  float x=((float)((i*1973)%40000)-20000.0f)*4.1f;
  float z=((float)((i*2671)%46000)-23000.0f)*3.7f;
  rh_set_player(&g,x,z,i*.53f);
  g.player.speed_abs=(i%5)*8.0f;g.sim_t=i*.33f;
  g.weather=i%4;g.clock_hours=(float)(i%24);g.weather_mode=i%5;g.time_mode=(i/5)%5;g.settings_row=i%2;
  g.player.camera=(i&1);g.map_open=(i%11==0);g.paused=(i%17==0);
  mock_3d=mock_2d=0;
  rh_render_frame(&g,(i%3)-1);
  assert(mock_3d>100&&mock_3d<54000);
  assert(mock_2d>200&&mock_2d<25001);
  if(mock_3d>max3)max3=mock_3d;
  if(mock_2d>max2)max2=mock_2d;
 }
 /* Every selectable vehicle is emitted, and leaving it switches to a visible
    third-person character without exceeding the mesh or overlay budgets. */
 g.paused=g.map_open=g.service_phase=0;g.job.stage=0;g.traffic_count=0;
 rh_set_player(&g,0,90,0);g.player.camera=0;g.clock_hours=12;g.weather=0;
 int transport_vertices[4];
 for(int mode=0;mode<4;mode++){
  g.transport_mode=mode;g.on_foot=0;g.sim_t+=1;
  mock_3d=mock_2d=0;rh_render_frame(&g,0);transport_vertices[mode]=mock_3d;
  assert(mock_3d>1000&&mock_3d<54000&&mock_2d<25000);
  g.on_foot=1;g.person_x=g.player.x+3;g.person_z=g.player.z;g.person_yaw=0;g.sim_t+=1;
  mock_3d=mock_2d=0;rh_render_frame(&g,0);
  assert(mock_3d>1000&&mock_3d<54000&&mock_2d<25000);
 }
 assert(transport_vertices[0]!=transport_vertices[1]);
 assert(transport_vertices[1]!=transport_vertices[2]);
 assert(transport_vertices[2]!=transport_vertices[3]);
 /* Cargo appears only on a vehicle actually carrying the active job. */
 for(int mode=1;mode<=3;mode++){
  g.on_foot=0;g.transport_mode=mode;g.parked_valid=0;g.job.stage=0;
  mock_3d=mock_2d=0;rh_render_frame(&g,0);int empty=mock_3d;
  g.job.stage=1;mock_3d=mock_2d=0;rh_render_frame(&g,0);int loaded=mock_3d;
  if(mode==3)assert(loaded==empty);else assert(loaded>empty);
  g.parked_valid=1;g.parked_x=100000;g.parked_z=100000;
  mock_3d=mock_2d=0;rh_render_frame(&g,0);assert(mock_3d==empty);
 }
 g.parked_valid=0;g.job.stage=0;

 /* The bicycle first-person view is at rider height, without the truck cab. */
 g.on_foot=0;g.player.camera=1;g.transport_mode=0;g.sim_t+=1;
 mock_3d=mock_2d=0;rh_render_frame(&g,0);int truck_cab_hud=mock_2d;
 g.transport_mode=3;g.sim_t+=1;mock_3d=mock_2d=0;rh_render_frame(&g,0);
 assert(mock_camera_eye.y>1.8f&&mock_camera_eye.y<2.0f);
 assert(mock_2d<truck_cab_hud);
 g.transport_mode=g.on_foot=0;
 /* Dense city blocks, outer edges, repeated city and nearby car LOD/lights. */
 const float urban[][2]={{2300,2300},{-2300,-2300},{14400,14400},{16700,12100},{2200,0},{-2401,0},{3200,3200},{720,900}};
 g.paused=g.map_open=g.service_phase=0;g.traffic_count=3;
 for(int i=0;i<8;i++)for(int camera=0;camera<2;camera++){
  rh_set_player(&g,urban[i][0],urban[i][1],0);g.player.camera=camera;
  g.clock_hours=camera?0:12;g.weather=i%4;
  for(int c=0;c<3;c++){
   g.cars[c].x=g.player.x+(c-1)*3;g.cars[c].z=g.player.z+14+c*18;
   g.cars[c].yaw=c==1?RH_PI:0;g.cars[c].variant=c==2?3:c;
  }
  mock_3d=mock_2d=0;rh_render_frame(&g,0);
  assert(mock_3d>1000&&mock_3d<54000&&mock_2d<25000);
  if(mock_3d>max3)max3=mock_3d;if(mock_2d>max2)max2=mock_2d;
 }
 /* Both bridge levels, approaches and biome interiors in both cameras. */
 const float scenes[][2]={{0,7200},{0,6850},{0,6650},{-250,7200},{0,23000},{0,40000},{0,55000}};
 for(unsigned i=0;i<sizeof(scenes)/sizeof(scenes[0]);i++)for(int camera=0;camera<2;camera++){
  rh_set_player(&g,scenes[i][0],scenes[i][1],i==3?RH_PI*.5f:0);
  g.player.road_level=i==3?0:1;g.player.camera=camera;g.job.stage=1;
  mock_3d=mock_2d=0;rh_render_frame(&g,0);
  assert(mock_3d>100&&mock_3d<54000&&mock_2d<25000);
  if(mock_3d>max3)max3=mock_3d;if(mock_2d>max2)max2=mock_2d;
 }
 /* Celestial geometry follows opposite sunrise/sunset world directions. */
 g.paused=g.map_open=g.weather=0;g.player.camera=1;g.player.speed_abs=0;
 rh_set_player(&g,0,90,RH_PI*.5f);g.sim_t+=1;g.clock_hours=6;
 mock_3d=mock_2d=0;rh_render_frame(&g,0);
 assert(mock_sun_vertices>20&&mock_moon_vertices==0);
 rh_set_player(&g,0,90,-RH_PI*.5f);g.sim_t+=1;g.clock_hours=18;
 mock_3d=mock_2d=0;rh_render_frame(&g,0);
 assert(mock_sun_vertices>20&&mock_moon_vertices==0);
 rh_set_player(&g,0,90,0);g.sim_t+=1;g.clock_hours=0;
 mock_3d=mock_2d=0;rh_render_frame(&g,0);
 assert(mock_sun_vertices==0&&mock_moon_vertices>20&&mock_star_vertices>0);
 /* Service van/progress and empty-tank map prompt across both cameras. */
 g.clock_hours=12;g.player.fuel=0;g.job.stage=1;
 for(int camera=0;camera<2;camera++)for(int map=0;map<2;map++){
  g.player.camera=camera;g.map_open=map;g.service_phase=0;
  mock_3d=mock_2d=0;rh_render_frame(&g,0);int no_van=mock_3d;
  for(int phase=1;phase<=3;phase++){
   g.service_phase=phase;g.service_timer=phase==1?24:phase==2?17:5;
   g.service_x=-4.5f;g.service_z=105;g.service_yaw=0;
   mock_3d=mock_2d=0;rh_render_frame(&g,0);
   assert(mock_3d>no_van&&mock_3d<54001&&mock_2d<25001);
   if(mock_3d>max3)max3=mock_3d;if(mock_2d>max2)max2=mock_2d;
  }
 }
 rh_render_end();printf("PASS renderer: terrain/camera/weather/celestial/service/urban/car/bridge/biome cases, near clipping, HUD GPU state and glyph coverage; maximum vertices 3D %d / 54000, HUD %d / 25000\n",max3,max2);return 0;
}
