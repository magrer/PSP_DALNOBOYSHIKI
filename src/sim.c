#include "sim.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>

const TruckSpec RH_TRUCKS[3] = {
 {"СЕВЕР 18", 7600, 3.65f, 1750, {12.1f, 8.4f, 5.9f, 4.2f, 2.9f, 1.9f, 1.3f, 1.0f},8,3.7f,.51f,430,.52f,0,7.0f},
 {"ТАЙГА 26", 8300, 3.85f, 2150, {11.7f, 8.2f, 5.7f, 4.0f, 2.8f, 1.85f, 1.25f, .96f},8,3.35f,.53f,600,.50f,28000,7.4f},
 {"АТЛАНТ 34", 9500, 4.10f, 2700, {13.1f, 9.1f, 6.3f, 4.4f, 3.0f, 2.0f, 1.34f, .95f},8,3.15f,.55f,800,.47f,65000,7.9f}
};
static float clampf(float v,float lo,float hi){return v<lo?lo:v>hi?hi:v;}

/* ===== Города ТЗ 2.0 ===== */
static const char *CITY_NAMES[4] = {"ПРИВОЛЬСК","СЕВЕРОДОНСК","ГРУЗОВОЙ","МОРСК"};
/* Примерные координаты узлов дорог (в клетках RH_ROAD_SPACING) */
static const int CITY_GX[4] = {0, 4, -3, 2};
static const int CITY_GZ[4] = {0, 3, 5, -4};

const char *rh_city_name(int idx){
 if(idx<0||idx>3) return "НЕИЗВЕСТНО";
 return CITY_NAMES[idx];
}
int rh_nearest_city(float x, float z){
 int best=0; float best_d=1e30f;
 for(int i=0;i<4;i++){
  float cx = CITY_GX[i]*RH_ROAD_SPACING;
  float cz = CITY_GZ[i]*RH_ROAD_SPACING;
  float d = rh_dist2(x,z,cx,cz);
  if(d<best_d){best_d=d;best=i;}
 }
 return best;
}

static float ab(float f){return f<0?-f:f;}
static float wrapped(float a){while(a>RH_PI)a-=2*RH_PI;while(a< -RH_PI)a+=2*RH_PI;return a;}
static uint32_t next_rng(Game *g) { uint32_t x=g->rng; x^=x<<13; x^=x>>17; x^=x<<5; return g->rng=x?x:238843u; }
static int rndi(Game *g,int n){return (int)(next_rng(g)%(uint32_t)n);}
float rh_dist2(float x,float z,float x2,float z2){float dx=x-x2,dz=z-z2;return sqrtf(dx*dx+dz*dz);}
static uint32_t world_hash(int x,int z){uint32_t h=(uint32_t)x*374761393u+(uint32_t)z*668265263u+12345u;h=(h^(h>>13))*1274126177u;return h^(h>>16);}
/* Bends vanish with zero slope at junctions, including negative coordinates. */
float rh_road_x(int gx,float z){float q=sinf(RH_PI*z/RH_ROAD_SPACING);return gx*RH_ROAD_SPACING+70*q*q*sinf(gx*1.7f+z*.00043f+.4f);}
float rh_road_z(int gz,float x){float q=sinf(RH_PI*x/RH_ROAD_SPACING);return gz*RH_ROAD_SPACING+65*q*q*sinf(gz*2.1f+x*.00051f+1.2f);}
int rh_biome(float x,float z){
 (void)x;float q=z+15000-60000*floorf((z+15000)/60000);
 return q<30000?0:q<50000?1:2;
}
int rh_bridge_info(int gx,int gz,float *x,float *z){
 if(((gx%8)+8)%8!=0||((gz%8)+8)%8!=4)return 0;
 if(x)*x=gx*RH_ROAD_SPACING;if(z)*z=gz*RH_ROAD_SPACING;return 1;
}
static int bridge_at(float x,float z,int *gx,int *gz){
 *gx=(int)floorf(x/RH_ROAD_SPACING+.5f);*gz=(int)floorf(z/RH_ROAD_SPACING+.5f);
 return rh_bridge_info(*gx,*gz,NULL,NULL)&&ab(z-*gz*RH_ROAD_SPACING)<=RH_BRIDGE_HALF_LENGTH&&
        ab(x-rh_road_x(*gx,z))<=RH_BRIDGE_HALF_WIDTH;
}
int rh_city_tile(int tx,int tz){
 int x=((tx+RH_CITY_BLOCKS/2)%30+30)%30;
 int z=((tz+RH_CITY_BLOCKS/2)%30+30)%30;
 float centerz=RH_CITY_SPACING*floorf(((tz+.5f)*RH_CITY_BLOCK)/RH_CITY_SPACING+.5f);
 if(rh_biome(0,centerz-2400)||rh_biome(0,centerz+2400))return 0;
 return x<RH_CITY_BLOCKS&&z<RH_CITY_BLOCKS;
}
float rh_cityroad_distance(float x,float z){
 int tx=(int)floorf(x/RH_CITY_BLOCK),tz=(int)floorf(z/RH_CITY_BLOCK);
 int urban=rh_city_tile(tx,tz);
 /* Outer boundary streets have the same shoulder width on both sides. */
 if(!urban){
  int lx=(int)floorf((x-7)/RH_CITY_BLOCK),rx=(int)floorf((x+7)/RH_CITY_BLOCK);
  int nz=(int)floorf((z-7)/RH_CITY_BLOCK),sz=(int)floorf((z+7)/RH_CITY_BLOCK);
  urban=rh_city_tile(lx,nz)||rh_city_tile(lx,sz)||rh_city_tile(rx,nz)||rh_city_tile(rx,sz);
 }
 if(!urban)return 9999;
 float dx=ab(x-RH_CITY_BLOCK*floorf(x/RH_CITY_BLOCK+.5f));
 float dz=ab(z-RH_CITY_BLOCK*floorf(z/RH_CITY_BLOCK+.5f));
 return fminf(dx,dz);
}
int rh_settlement_kind(int gx,int gz){
 if(rh_city_tile((int)floorf(gx*RH_ROAD_SPACING/RH_CITY_BLOCK),
                 (int)floorf(gz*RH_ROAD_SPACING/RH_CITY_BLOCK)))return 2;
 int biome=rh_biome(gx*RH_ROAD_SPACING,gz*RH_ROAD_SPACING);
 if(biome!=0)return 0;
 return world_hash(gx,gz)%4u==0?1:0;
}
float rh_road_distance(float x,float z) {
 int cx=(int)floorf(x/RH_ROAD_SPACING+.5f),cz=(int)floorf(z/RH_ROAD_SPACING+.5f);
 float dx=ab(x-rh_road_x(cx,z)),dz=ab(z-rh_road_z(cz,x));
 float zz=z-cz*RH_ROAD_SPACING;
 float xx=x-rh_road_x(cx,z);
 float spur_a=(xx>=-7 && xx<=124)?ab(zz-135):9999;
 float spur_b=(xx>=-124 && xx<=7)?ab(zz+130):9999;
 float d=fminf(dx,dz);return fminf(rh_cityroad_distance(x,z),fminf(d,fminf(spur_a,spur_b)));
}
int rh_water(float x,float z){
 int gx=(int)floorf(x/RH_ROAD_SPACING+.5f),gz=(int)floorf(z/RH_ROAD_SPACING+.5f);
 float bx,bz;
 if(rh_bridge_info(gx,gz,&bx,&bz)&&ab(x-bx)<260&&ab(z-bz)<260&&ab(z-bz)>12)return 1;
 float river=720+110*sinf(z*.00065f);
 float river_delta=x-river-7200*floorf((x-river)/7200+.5f);
 if(ab(river_delta)<30)return 2;
 float lx=x-1100-7200*floorf((x-1100)/7200+.5f);
 float lz=z-950-5400*floorf((z-950)/5400+.5f);
 return lx*lx/(180*180)+lz*lz/(270*270)<1?1:0;
}
int rh_tree_at(int cx,int cz,float *x,float *z){
 uint32_t h=world_hash(cx,cz);*x=cx*40+5+(h%300)/10.0f;*z=cz*40+5+((h>>10)%300)/10.0f;
 int gx=(int)floorf(*x/RH_ROAD_SPACING+.5f),gz=(int)floorf(*z/RH_ROAD_SPACING+.5f);
 int biome=rh_biome(*x,*z);
 if((biome==0?h%5u!=0:biome==1?h%10u==0:h%12u!=0)||rh_road_distance(*x,*z)<22||rh_water(*x,*z))return 0;
 if(rh_dist2(*x,*z,gx*RH_ROAD_SPACING,gz*RH_ROAD_SPACING)<230)return 0;
 return 1;
}
static float raw_height(float x,float z) {
 return 4.3f*sinf(x*.00155f)+5.6f*cosf(z*.00125f)+
  3.0f*sinf((x+z)*.0018f)+1.6f*sinf(x*.0081f)*cosf(z*.0072f);
}
static float mountain_weight(float z){
 float q=z+15000-60000*floorf((z+15000)/60000);
 if(q<50000)return 0;
 float t=clampf(fminf(q-50000,60000-q)/2000,0,1);return t*t*(3-2*t);
}
float rh_height(float x,float z) {
 // Keep a smooth road-grade profile under roads and access spurs.
 float d=rh_road_distance(x,z);float smooth=clampf((d-7)/37.0f,0,1);
 float road=4.3f*sinf(x*.00155f)+5.6f*cosf(z*.00125f)+3.0f*sinf((x+z)*.0018f);
 float mountain=mountain_weight(z);
 float pass=mountain*(14+8*sinf(x*.00027f)+5*cosf(z*.00031f));
 float ridge=mountain*(140+65*sinf(x*.00065f)*cosf(z*.00042f)+35*cosf(x*.0011f+z*.0008f));
 int gx=(int)floorf(x/RH_ROAD_SPACING+.5f),gz=(int)floorf(z/RH_ROAD_SPACING+.5f);
 if(ridge>0&&rh_bridge_info(gx,gz,NULL,NULL)){
  float edge=fmaxf(ab(x-gx*RH_ROAD_SPACING)-260,ab(z-gz*RH_ROAD_SPACING)-300);
  float valley=clampf(edge/250,0,1);ridge*=valley*valley*(3-2*valley);
 }
 return road+pass+smooth*(raw_height(x,z)-road+ridge);
}
float rh_bridge_deck(int gx,int gz,float z){
 float center=gz*RH_ROAD_SPACING,low=center-RH_BRIDGE_HALF_LENGTH,high=center+RH_BRIDGE_HALF_LENGTH;
 float t=clampf((z-low)/(2*RH_BRIDGE_HALF_LENGTH),0,1);
 float y0=rh_height(rh_road_x(gx,low),low),y1=rh_height(rh_road_x(gx,high),high);
 float ramp=clampf((RH_BRIDGE_HALF_LENGTH-ab(z-center))/330,0,1);
 return y0+(y1-y0)*t+18*ramp*ramp*(3-2*ramp);
}
float rh_road_height(float x,float z,int axis){
 int gx,gz;if(axis==0&&bridge_at(x,z,&gx,&gz))return rh_bridge_deck(gx,gz,z);
 return rh_height(x,z);
}
float rh_vehicle_height(float x,float z,float yaw){return rh_road_height(x,z,ab(cosf(yaw))>=.7071f?0:1);}
float rh_player_height(const Game *g){return rh_road_height(g->player.x,g->player.z,g->player.road_level?0:1);
}
void rh_depot(int gx,int gz,float *x,float *z) {*z=(float)gz*RH_ROAD_SPACING+135;*x=rh_road_x(gx,*z)+114;}
void rh_station(int gx,int gz,float *x,float *z) {*z=(float)gz*RH_ROAD_SPACING-130;*x=rh_road_x(gx,*z)-113;}
static void trailer_place(Player *p){
 p->trailer_x=p->x-sinf(p->yaw)*RH_TRAILER_HITCH-sinf(p->trailer_yaw)*RH_TRAILER_CENTER;
 p->trailer_z=p->z-cosf(p->yaw)*RH_TRAILER_HITCH-cosf(p->trailer_yaw)*RH_TRAILER_CENTER;
}
void rh_set_player(Game *g,float x,float z,float yaw){g->player.x=x;g->player.z=z;g->player.yaw=yaw;g->player.v=0;g->player.speed_abs=0;g->player.reverse=0;g->player.gearbox_t=0;g->player.engine_t=0.3f;g->player.trailer_yaw=yaw;g->player.lateral_v=0;
 int gx,gz;g->player.road_level=ab(cosf(yaw))>=.7071f&&bridge_at(x,z,&gx,&gz);
 trailer_place(&g->player);}
static void init_job(Game *g,int px,int pz) {
 Job *j=&g->job;int dx=0,dz=0,attempt;
 for(attempt=0;attempt<30;attempt++){
  dx=rndi(g,13)-6;dz=rndi(g,13)-6;
  if(abs(dx)+abs(dz)>=5 && abs(dx)+abs(dz)<=10)break;
 }
 if(abs(dx)+abs(dz)<5){dx=4;dz=3;}
 j->pickup_x=px;j->pickup_z=pz;j->dest_x=px+dx;j->dest_z=pz+dz;
 j->cargo_tons=8+rndi(g,20);j->type=rndi(g,4);j->stage=0;
 j->distance_km=(ab((float)dx)+ab((float)dz))*RH_ROAD_SPACING/1000.0f;
 j->payout=(850.0f+35.0f*j->cargo_tons)*j->distance_km;
}
/* sceGumLookAt forward +Z, up +Y maps screen-right to world -X.
   The road lane normal therefore is (-cos(yaw), sin(yaw)). */
static void spawn_traffic(Game *g,int i){
 Traffic *t=&g->cars[i];Player *p=&g->player;
 int axis=rndi(g,2),dir=rndi(g,2)?1:-1;
 int roadx=(int)floorf(p->x/RH_ROAD_SPACING+.5f)+(rndi(g,3)-1);
 int roadz=(int)floorf(p->z/RH_ROAD_SPACING+.5f)+(rndi(g,3)-1);
 float offset=130+rndi(g,750);if(rndi(g,2))offset=-offset;
 t->axis=axis;t->direction=dir;t->variant=rndi(g,4);
 /* Populate the street closest to the player in cities, not distant highways. */
 if(rh_city_tile((int)floorf(p->x/RH_CITY_BLOCK),(int)floorf(p->z/RH_CITY_BLOCK)) && i%4!=0){
  float along=65+(i%11)*22; if(i&1)along=-along;
  t->axis=axis+2;
  if(axis==0){t->x=RH_CITY_BLOCK*floorf(p->x/RH_CITY_BLOCK+.5f)-dir*2.8f;t->z=p->z+along;t->yaw=dir>0?0:RH_PI;}
  else{t->z=RH_CITY_BLOCK*floorf(p->z/RH_CITY_BLOCK+.5f)+dir*2.8f;t->x=p->x+along;t->yaw=dir>0?RH_PI/2:-RH_PI/2;}
  t->speed=(12.0f+rndi(g,120)*.1f)*dir;return;
 }
 if(axis==0){t->x=rh_road_x(roadx,p->z+offset)+(dir>0?-2.8f:2.8f);t->z=p->z+offset;t->yaw=dir>0?0:RH_PI;}
 else {t->z=rh_road_z(roadz,p->x+offset)+(dir>0?2.8f:-2.8f);t->x=p->x+offset;t->yaw=dir>0?RH_PI/2:-RH_PI/2;}
 t->speed=(12.0f+rndi(g,120)*.1f)*dir;
}
void rh_enter_garage(Game *g){
 g->state = ST_GARAGE;
 g->garage_cam_yaw = 0.9f;
 g->garage_cam_dist = 14.0f;
 g->garage_cam_height = 5.0f;
 g->computer_sel = 0;
 {
  int ci = g->home_city % 4;
  float wz = CITY_GZ[ci] * RH_ROAD_SPACING;
  float wx = rh_road_x(CITY_GX[ci], wz) - 3.0f;
  rh_set_player(g, wx, wz, 0.4f);
 }
 g->player.v = 0;
 g->player.speed_abs = 0;
 g->player.rpm = 650;
 if(g->player.fuel < 10.0f) g->player.fuel = RH_TRUCKS[g->player.model].tank_l;
 g->on_foot = 0;
 g->paused = 0;
 g->map_open = 0;
 g->transport_mode = 0;
 g->player.camera = 0; /* 3-е лицо для осмотра */
}

void rh_leave_garage(Game *g){
 /* Без телепорта: стоим там же, где стояли в гараже.
    Резкий прыжок + полный мир = краш на PSP-3000. */
 g->player.v = 0;
 g->player.speed_abs = 0;
 g->player.rpm = 800;
 g->player.camera = 0;
 g->player.reverse = 0;
 g->player.road_level = 0;
 g->on_foot = 0;
 g->transport_mode = 0;
 g->map_open = 0;
 g->paused = 0;
 /* меньше машин на выезде */
 if(g->traffic_count > 4) g->traffic_count = 4;
 g->state = ST_PLAY;
}

void rh_new_game(Game *g){
 memset(g,0,sizeof(*g));
 g->player.engine_t=0.3f;
 g->player.gearbox_t=0.25f;
 if(g->player.model < 0 || g->player.model > 2) g->player.model = 0;
 g->rng=0x786423ad;
 g->bank=6500;
 g->clock_hours=8.0f;
 g->owned_trucks=1u;
 g->weather_next_odo=3+rndi(g,8);
 g->player.model=0;
 g->player.fuel=RH_TRUCKS[0].tank_l;
 g->home_city = 1;
 g->current_city = 1;
 {
  float wz = CITY_GZ[1] * RH_ROAD_SPACING;
  float wx = rh_road_x(CITY_GX[1], wz) - 3.0f;
  rh_set_player(g, wx, wz, 0.4f);
 }
 g->player.rpm=650;
 g->player.gear=1;
 init_job(g, CITY_GX[1], CITY_GZ[1]);
 g->job.stage = -1; /* нет активного заказа до компьютера */
 g->traffic_count = 4;
 if(g->traffic_count > 22) g->traffic_count = 22;
 if(g->traffic_count < 0) g->traffic_count = 0;
 for(int i=0;i<g->traffic_count;i++)spawn_traffic(g,i);
 g->state = ST_GARAGE;
 g->menu_timer = 0;
 g->menu_blink = 1;
 g->garage_cam_yaw = 0.9f;
 g->garage_cam_dist = 14.0f;
 g->garage_cam_height = 5.0f;
 g->radio_station = 0;
 g->radio_open = 0;
 g->approach_dot = 1.0f;
 g->player.camera = 0;
}
static float engine_torque(const TruckSpec *t,float rpm){
 // Plateau near 1100-1500 RPM, modest low-end and high-RPM fall-off.
 float f= rpm<700?.58f+rpm/700*.22f : rpm<1150?.8f+(rpm-700)/450*.2f :
     rpm<1500?1.0f : 1.0f-(rpm-1500)/900*.49f;
 return t->torque_nm*clampf(f,.35f,1.0f);
}
/* Collision footprints use full oriented rectangles, not center distances. */
static int body_box(float x,float z,float yaw,float halfw,float halfl,float bx,float bz,float bw,float bl){
 float sx=sinf(yaw),cz=cosf(yaw),dx=bx-x,dz=bz-z;
 float ax=ab(sx),az=ab(cz),hx=bw*.5f,hz=bl*.5f;
 if(ab(dx)>halfw*az+halfl*ax+hx||ab(dz)>halfw*ax+halfl*az+hz)return 0;
 if(ab(dx*cz-dz*sx)>halfw+hx*az+hz*ax)return 0;
 if(ab(dx*sx+dz*cz)>halfl+hx*ax+hz*az)return 0;
 return 1;
}
static int bodies_overlap(float x,float z,float yaw,float hw,float hl,
                          float ox,float oz,float oyaw,float ow,float ol){
 /* Conservative world AABB first; only nearby pairs pay for SAT trigonometry. */
 float dx=x-ox,dz=z-oz,reach=hw+hl+ow+ol;
 if(ab(dx)>reach||ab(dz)>reach)return 0;
 float si=sinf(oyaw),co=cosf(oyaw);
 return body_box(dx*co-dz*si,dx*si+dz*co,yaw-oyaw,hw,hl,0,0,ow*2,ol*2);
}
/* draw_truck chassis reaches local Z=-4.2 and cab extends to +3.05.
   A centered conservative footprint covers every rendered truck model. */
#define RH_TRUCK_HALF_LENGTH 4.3f
static float traffic_half_length(const Traffic *t){return t->variant==3?RH_TRUCK_HALF_LENGTH:2.3f;}
static float traffic_half_width(const Traffic *t){return t->variant==3?1.25f:.975f;}
static float player_half_width(const Game *g){return g->transport_mode==3?.4f:g->transport_mode?1.0f:1.25f;}
static float player_half_length(const Game *g){return g->transport_mode==3?1.0f:g->transport_mode?2.3f:RH_TRUCK_HALF_LENGTH;}
static int traffic_hits_parked(const Game *g,const Traffic *t){
 if(!g->parked_valid)return 0;
 float ground=rh_road_height(g->parked_x,g->parked_z,g->parked_level?0:1);
 if(ab(rh_road_height(t->x,t->z,t->axis)-ground)>4)return 0;
 float tx=g->parked_x-sinf(g->parked_yaw)*RH_TRAILER_HITCH-sinf(g->parked_trailer_yaw)*RH_TRAILER_CENTER;
 float tz=g->parked_z-cosf(g->parked_yaw)*RH_TRAILER_HITCH-cosf(g->parked_trailer_yaw)*RH_TRAILER_CENTER;
 return bodies_overlap(t->x,t->z,t->yaw,traffic_half_width(t),traffic_half_length(t),g->parked_x,g->parked_z,g->parked_yaw,1.25f,RH_TRUCK_HALF_LENGTH)||
        bodies_overlap(t->x,t->z,t->yaw,traffic_half_width(t),traffic_half_length(t),tx,tz,g->parked_trailer_yaw,1.3f,4.3f);
}
static int traffic_hits_player(const Game *g,const Traffic *t){
 const Player *p=&g->player;float hw=traffic_half_width(t),hl=traffic_half_length(t);
 if(ab(t->x-p->x)>25||ab(t->z-p->z)>25)return 0;
 if(ab(rh_road_height(t->x,t->z,t->axis)-rh_player_height(g))>4)return 0;
 if(bodies_overlap(t->x,t->z,t->yaw,hw,hl,p->x,p->z,p->yaw,player_half_width(g),player_half_length(g)))return 1;
 return g->transport_mode==0&&g->job.stage==1&&bodies_overlap(t->x,t->z,t->yaw,hw,hl,p->trailer_x,p->trailer_z,p->trailer_yaw,1.3f,4.3f);
}
static int player_hits_traffic(const Game *g){
 for(int i=0;i<g->traffic_count;i++)if(traffic_hits_player(g,&g->cars[i]))return i;
 return -1;
}
static void vehicle_impact(Game *g,Traffic *t){
 Player *p=&g->player;
 float px=p->x,pz=p->z,py=p->yaw,hw=1.25f,hl=RH_TRUCK_HALF_LENGTH;
 if(g->transport_mode==0&&g->job.stage==1&&rh_dist2(t->x,t->z,p->trailer_x,p->trailer_z)<rh_dist2(t->x,t->z,px,pz)){
  px=p->trailer_x;pz=p->trailer_z;py=p->trailer_yaw;hw=1.3f;hl=4.3f;
 }
 float ps=sinf(py),pc=cosf(py),ts=sinf(t->yaw),tc=cosf(t->yaw);
 float axes[4][2]={{pc,-ps},{ps,pc},{tc,-ts},{ts,tc}},best=1e9f,nx=0,nz=1;
 for(int i=0;i<4;i++){
  float ax=axes[i][0],az=axes[i][1];
  float pa=hw*ab(ax*pc-az*ps)+hl*ab(ax*ps+az*pc);
  float ta=traffic_half_width(t)*ab(ax*tc-az*ts)+traffic_half_length(t)*ab(ax*ts+az*tc);
  float projection=(t->x-px)*ax+(t->z-pz)*az,overlap=pa+ta-ab(projection);
  if(overlap<best){best=overlap;float sign=projection<0?-1:1;nx=ax*sign;nz=az*sign;}
 }
 float si=sinf(p->yaw),co=cosf(p->yaw);
 float pvx=si*p->v+co*p->lateral_v,pvz=co*p->v-si*p->lateral_v;
 float nv=ab(t->speed),nvx=ts*nv,nvz=tc*nv;
 float closing=(pvx-nvx)*nx+(pvz-nvz)*nz;
 if(closing<=.01f)return;
 float pm=RH_TRUCKS[p->model].mass_kg+(g->job.stage==1?4600+g->job.cargo_tons*1000:0);
 float nm=t->variant==3?7600:1500;
 float impulse=1.04f*closing/(1/pm+1/nm);
 pvx-=impulse*nx/pm;pvz-=impulse*nz/pm;
 nvx+=impulse*nx/nm;nvz+=impulse*nz/nm;
 p->v=pvx*si+pvz*co;p->lateral_v=(pvx*co-pvz*si)*.85f;
 if(ab(p->v)<.08f)p->v=0;
 p->speed_abs=ab(p->v);if(p->v<0)p->reverse=1;
 t->speed=t->direction*clampf(nvx*ts+nvz*tc,0,25);
 /* Energy and mass ratio govern damage, not the number of contact frames. */
 p->damage=fminf(100,p->damage+closing*closing*.025f*nm/(pm+nm));
}
static float avoid_body(const Traffic *t,float desired,float x,float z,float yaw,float hw,float hl){
 float dx=x-t->x,dz=z-t->z;
 if(ab(dx)>90||ab(dz)>90)return desired;

 float hx=sinf(t->yaw),hz=cosf(t->yaw),ahead=dx*hx+dz*hz;
 float rel=yaw-t->yaw,si=ab(sinf(rel)),co=ab(cosf(rel));
 float along=hl*co+hw*si,side=hw*co+hl*si;
 if(ahead+along>0&&ahead<80&&ab(dx*hz-dz*hx)<side+traffic_half_width(t)+.35f){
  float gap=ahead-along-traffic_half_length(t);
  desired=fminf(desired,clampf((gap-2)*.68f,0,desired));
 }
 return desired;
}
static int body_blocked(float x,float z,float yaw,float halfw,float halfl,int level){
 float sx=sinf(yaw),cz=cosf(yaw);
 /* Corners plus center keep bridge clearance consistent with road width. */
 for(int i=0;i<5;i++){
  float side=i==4?0:((i&1)?halfw:-halfw),along=i==4?0:((i&2)?halfl:-halfl);
  float px=x+cz*side+sx*along,pz=z-sx*side+cz*along;
  int gx,gz;int deck=level&&bridge_at(px,pz,&gx,&gz);
  if(rh_water(px,pz)&&!deck&&(rh_road_distance(px,pz)>10||bridge_at(px,pz,&gx,&gz)))return 1;
  if(level&&!deck&&ab(rh_road_height(x,z,0)-rh_height(x,z))>.8f)return 1;
 }
 float extentx=ab(cz)*halfw+ab(sx)*halfl+.85f;
 float extentz=ab(sx)*halfw+ab(cz)*halfl+.85f;
 int cx=(int)floorf(x/40),ccz=(int)floorf(z/40);
 for(int iz=ccz-1;iz<=ccz+1;iz++)for(int ix=cx-1;ix<=cx+1;ix++){
  /* Reject candidate tree positions before road/water terrain queries. */
  uint32_t h=world_hash(ix,iz);
  float tx=ix*40+5+(h%300)/10.0f,tz=iz*40+5+((h>>10)%300)/10.0f;
  if(ab(tx-x)>extentx||ab(tz-z)>extentz)continue;
  if(!rh_tree_at(ix,iz,&tx,&tz))continue;
  float dx=tx-x,dz=tz-z;
  float side=fmaxf(0,ab(dx*cz-dz*sx)-halfw),along=fmaxf(0,ab(dx*sx+dz*cz)-halfl);
  if(side*side+along*along<.85f*.85f)return 1;
 }
 int gx=(int)floorf(x/RH_ROAD_SPACING+.5f),gz=(int)floorf(z/RH_ROAD_SPACING+.5f);
 float bx=gx*RH_ROAD_SPACING,bz=gz*RH_ROAD_SPACING;
 int kind=rh_settlement_kind(gx,gz),count=kind==1?5:0;
 for(int k=0;k<count;k++){
  float xx=bx+(k%4-1.5f)*66,zz=bz+(k/4<2?-1:1)*(210+(k/4%2)*71);
  /* Most settlement buildings are hundreds of meters from this body. */
  float width=kind==2?24:17,length=kind==2?28:20;
  if(ab(xx-x)>extentx+width*.5f||ab(zz-z)>extentz+length*.5f)continue;
  if(!body_box(x,z,yaw,halfw,halfl,xx,zz,width,length))continue;
  if(rh_road_distance(xx,zz)>=23&&!rh_water(xx,zz))return 1;
 }
 /* Only adjacent city tiles can touch this footprint; the 100-tile city
    never becomes a global per-frame collision scan. */
 int cityx=(int)floorf(x/RH_CITY_BLOCK),cityz=(int)floorf(z/RH_CITY_BLOCK);
 for(int tz=cityz-1;tz<=cityz+1;tz++)for(int tx=cityx-1;tx<=cityx+1;tx++){
  if(!rh_city_tile(tx,tz))continue;
  float centerx=(tx+.5f)*RH_CITY_BLOCK,centerz=(tz+.5f)*RH_CITY_BLOCK;
  if(ab(centerx-x)>111+extentx||ab(centerz-z)>113+extentz)continue;
  for(int k=0;k<16;k++){
   float xx=centerx+(k%4-1.5f)*66,zz=centerz+(k/4-1.5f)*66;
   if(ab(xx-x)>extentx+12||ab(zz-z)>extentz+14)continue;
   if(body_box(x,z,yaw,halfw,halfl,xx,zz,24,28)&&rh_road_distance(xx,zz)>=23&&!rh_water(xx,zz))return 1;
  }
 }
 float depotx,depotz;rh_depot(gx,gz,&depotx,&depotz);
 if(body_box(x,z,yaw,halfw,halfl,depotx+3,depotz+40,64,36)||
    body_box(x,z,yaw,halfw,halfl,depotx+51,depotz-21,25,35)||
    body_box(x,z,yaw,halfw,halfl,bx-94,bz-130,29,12))return 1;
 return 0;
}
/* Find an unobstructed lane while keeping the coupled trailer on the same deck. */
static void recover_stuck(Game *g){
 Player original=g->player,best=original;float bestdist=1e30f;
 int gx=(int)floorf(original.x/RH_ROAD_SPACING+.5f),gz=(int)floorf(original.z/RH_ROAD_SPACING+.5f);
 for(int axis=0;axis<2;axis++)for(int d=-1;d<=1;d+=2)for(int j=-8;j<=8;j++){
  float x,z,yaw;
  if(axis==0){z=original.z+j*40.0f;x=rh_road_x(gx,z)-d*2.8f; yaw=atan2f(rh_road_x(gx,z+1)-rh_road_x(gx,z-1),2)+(d<0?RH_PI:0);}
  else{x=original.x+j*40.0f;z=rh_road_z(gz,x)+d*2.8f; yaw=atan2f(2,rh_road_z(gz,x+1)-rh_road_z(gz,x-1))+(d<0?RH_PI:0);}
  float distance=rh_dist2(original.x,original.z,x,z);if(distance>=bestdist)continue;
  rh_set_player(g,x,z,yaw);Player *p=&g->player;
  if(body_blocked(p->x,p->z,p->yaw,1.25f,RH_TRUCK_HALF_LENGTH,p->road_level)||
     (g->job.stage==1&&body_blocked(p->trailer_x,p->trailer_z,p->trailer_yaw,1.3f,4.3f,p->road_level)))continue;
  best=*p;bestdist=distance;
 }
 g->player=best;
 if(bestdist<1e30f){
  for(int i=0;i<g->traffic_count;i++)if(rh_dist2(g->cars[i].x,g->cars[i].z,best.x,best.z)<100)spawn_traffic(g,i);
 }
}
static void physics_step(Game *g,Controls in,float dt) {
 Player *p=&g->player;const TruckSpec *t=&RH_TRUCKS[p->model];
 float mass=t->mass_kg+(g->job.stage==1?4600.0f+(float)g->job.cargo_tons*1000.0f:0.0f);
 float old_v=p->v,speed=ab(old_v),road_dist=rh_road_distance(p->x,p->z);
 float steer=clampf(in.steer,-1,1),throttle=clampf(in.throttle,0,1);
 float brake=clampf(in.brake,0,1);
 if(in.reverse_request && speed<.35f && throttle<.15f)p->reverse=!p->reverse;
 if(throttle>.15f)p->reverse=0;
 if(p->reverse){throttle=brake;brake=clampf(in.throttle,0,1);}
 /* The map holds the brakes in either gear; never reinterpret them as reverse throttle. */
 if(g->map_open){throttle=0;brake=1;}
 /* Diesel pedal and turbo response: a short but tangible ramp. */
 p->engine_t+=clampf(throttle-p->engine_t,-dt*3.5f,dt*2.8f);
 p->engine_t=clampf(p->engine_t,0,1);
 float wheelrpm=speed*60.0f/(6.2831853f*t->wheel_radius);
 float ratio=t->gears[p->gear-1]*t->final_drive;
 float idealrpm=wheelrpm*ratio;
 float targetrpm=clampf(fmaxf(620,idealrpm)+p->engine_t*90.0f,620,2200);
 p->rpm+=clampf((targetrpm-p->rpm)*dt*4.2f,-dt*1400,dt*1650);
 /* The automatic transmission briefly interrupts power when shifting. */
 if(p->gearbox_t>0)p->gearbox_t=fmaxf(0,p->gearbox_t-dt);
 else if(p->rpm>1830 && p->gear<t->n_gears && speed>3.0f){
  p->gear++;p->gearbox_t=.37f;p->rpm=clampf(p->rpm*t->gears[p->gear-1]/t->gears[p->gear-2],650,1800);
 }else if(p->rpm<900 && p->gear>1 && speed>1.7f){
  p->gear--;p->gearbox_t=.29f;p->rpm=clampf(p->rpm*t->gears[p->gear-1]/t->gears[p->gear],650,2050);
 }
 if(speed<.35f && p->gear!=1){p->gear=1;p->gearbox_t=0;}
 ratio=t->gears[p->gear-1]*t->final_drive;
 float torque=engine_torque(t,p->rpm)*p->engine_t;
 /* Nominal diesel power limits: NORD 280 kW, BOREAL 350, ATLAS 430.
    A heavy laden tractor cannot accelerate as if its torque were infinite. */
 const float maxkw[3]={280.0f,350.0f,430.0f};
 float wheel_force=torque*ratio*.98f/t->wheel_radius;
 float powercap=maxkw[p->model]*1000.0f/fmaxf(speed,5.5f);
 wheel_force=fminf(wheel_force,powercap);
 const float traction[4]={1.0f,.72f,.43f,.16f};
 float surface=traction[g->weather>=0&&g->weather<4?g->weather:0];
 float grip=(road_dist<8.0f?.39f:road_dist<20.0f?.18f:.11f)*surface;
 wheel_force=fminf(wheel_force,grip*mass*9.81f);
 if(p->gearbox_t>0)wheel_force*=.16f;
 wheel_force*=1.0f-.006f*p->damage;
 if(p->fuel<.01f)wheel_force=0;
 float slope=(rh_road_height(p->x+sinf(p->yaw)*2.0f,p->z+cosf(p->yaw)*2.0f,p->road_level?0:1)-
              rh_road_height(p->x-sinf(p->yaw)*2.0f,p->z-cosf(p->yaw)*2.0f,p->road_level?0:1))/4.0f;
 float rolling=mass*9.81f*(road_dist<8.0f?.009f:.049f);
 float drag=.74f*speed*speed;
 /* 0.44-0.65 g road deceleration: air brakes + parking brake. */
 float braking=mass*9.81f*fminf(brake*.49f+(in.handbrake?.18f:0),.65f*surface);
 if(road_dist>9.0f)braking*=.68f;
 float brake_force=braking+rolling+drag;
 float grade_force=mass*9.81f*slope;
 /* Signed speed: negative only in selected reverse. */
 float direction=p->reverse?-1.0f:1.0f;
 float effective=wheel_force-brake_force-direction*grade_force;
 float speed_new=fmaxf(0.0f,speed+effective/mass*dt);
 if(p->fuel<.01f)speed_new=fmaxf(0,speed_new-1.3f*dt);
 speed_new=fminf(speed_new,p->reverse?4.6f:25.0f);
 float v=direction*speed_new;
 /* Reduce steering authority at speed and smooth yaw under heavy loads. */
 float steer_angle=steer*t->max_steer_rad/(1.0f+speed*.055f);
 float yaw_rate=v/t->wheelbase_m*tanf(steer_angle)*.67f;
 if(road_dist>12.0f)yaw_rate*=.74f;
 /* Tire lateral force saturates on ice: steering continues to turn the cab,
    while lateral momentum decays according to available road adhesion. */
 float yaw_cap=9.81f*surface*.65f/fmaxf(speed,1);
 yaw_rate=clampf(yaw_rate,-yaw_cap,yaw_cap);
 float oldx=p->x,oldz=p->z,oldyaw=p->yaw,oldtrailer=p->trailer_yaw;
 int oldlevel=p->road_level;float oldheight=rh_player_height(g);
 float hitchx=p->x-sinf(p->yaw)*RH_TRAILER_HITCH;
 float hitchz=p->z-cosf(p->yaw)*RH_TRAILER_HITCH;
 float ax=hitchx-sinf(p->trailer_yaw)*RH_TRAILER_AXLE;
 float az=hitchz-cosf(p->trailer_yaw)*RH_TRAILER_AXLE;
 p->lateral_v-=v*yaw_rate*dt;
 p->lateral_v*=expf(-surface*7*dt);
 p->yaw=wrapped(p->yaw+yaw_rate*dt);
 p->x+=(sinf(p->yaw)*v+cosf(p->yaw)*p->lateral_v)*dt;
 p->z+=(cosf(p->yaw)*v-sinf(p->yaw)*p->lateral_v)*dt;
 p->v=v;p->speed_abs=ab(v);

 if(g->job.stage==1){
  /* Project the previous trailer axle onto its rigid drawbar circle.
     Signed tractor motion makes reversing unstable, as a real trailer is. */
  hitchx=p->x-sinf(p->yaw)*RH_TRAILER_HITCH;
  hitchz=p->z-cosf(p->yaw)*RH_TRAILER_HITCH;
  float dx=hitchx-ax,dz=hitchz-az;
  if(dx*dx+dz*dz>.001f)p->trailer_yaw=atan2f(dx,dz);
  float angle=wrapped(p->yaw-p->trailer_yaw);
  if(ab(angle)>1.40f){p->trailer_yaw=wrapped(p->yaw-clampf(angle,-1.40f,1.40f));p->v*=.9f;p->speed_abs=ab(p->v);}
 }else p->trailer_yaw=p->yaw;
 trailer_place(p);
 /* A stationary footprint cannot enter a new obstacle. Keep engine, fuel,
    trailer constraint, traffic and clock updates running while stopped. */
 int gx,gz,overbridge=bridge_at(p->x,p->z,&gx,&gz);
 if(!p->road_level&&overbridge&&ab(p->z-gz*RH_ROAD_SPACING)>500&&
    ab(rh_bridge_deck(gx,gz,p->z)-rh_height(p->x,p->z))<.8f)p->road_level=1;
 if(p->road_level&&!overbridge){
  int ogx,ogz;
  if(bridge_at(oldx,oldz,&ogx,&ogz)&&ab(oldheight-rh_height(oldx,oldz))<.8f)p->road_level=0;
 }
 int height_blocked=ab(rh_player_height(g)-oldheight)>fmaxf(.4f,rh_dist2(oldx,oldz,p->x,p->z)*.25f);
 int collision_car=player_hits_traffic(g);
 /* A heavy tractor can push a car along its lane, but only into free space. */
 if(collision_car>=0){
  Traffic *car=&g->cars[collision_car],previous=*car;
  float dx=p->x-oldx,dz=p->z-oldz,along=(car->axis&1)?dx:dz;
  if(ab(along)>.00001f)along+=along>0?.08f:-.08f;
  if(ab(along)>.00001f){
   if(car->axis&1){car->x+=along;int rz=(int)floorf(car->z/(car->axis>=2?RH_CITY_BLOCK:RH_ROAD_SPACING)+.5f);car->z=(car->axis>=2?rz*RH_CITY_BLOCK:rh_road_z(rz,car->x))+car->direction*2.8f;}
   else{car->z+=along;int rx=(int)floorf(car->x/(car->axis>=2?RH_CITY_BLOCK:RH_ROAD_SPACING)+.5f);car->x=(car->axis>=2?rx*RH_CITY_BLOCK:rh_road_x(rx,car->z))-car->direction*2.8f;}
   int bgx,bgz;int level=car->axis==0&&bridge_at(car->x,car->z,&bgx,&bgz);
   int blocked=traffic_hits_player(g,car)||body_blocked(car->x,car->z,car->yaw,traffic_half_width(car),traffic_half_length(car),level);
   for(int j=0;j<g->traffic_count&&!blocked;j++)if(j!=collision_car){Traffic *o=&g->cars[j];
    if(ab(rh_road_height(car->x,car->z,car->axis)-rh_road_height(o->x,o->z,o->axis))<4&&
       bodies_overlap(car->x,car->z,car->yaw,traffic_half_width(car),traffic_half_length(car),o->x,o->z,o->yaw,traffic_half_width(o),traffic_half_length(o)))blocked=1;
   }
   if(blocked)*car=previous;else{vehicle_impact(g,car);collision_car=player_hits_traffic(g);}
  }
 }
 int moved=ab(p->x-oldx)>1e-6f||ab(p->z-oldz)>1e-6f||ab(p->yaw-oldyaw)>1e-6f;
 if(moved&&(height_blocked||body_blocked(p->x,p->z,p->yaw,1.25f,RH_TRUCK_HALF_LENGTH,p->road_level)||
    (g->job.stage==1&&body_blocked(p->trailer_x,p->trailer_z,p->trailer_yaw,1.30f,4.30f,p->road_level))||collision_car>=0)){
  p->x=oldx;p->z=oldz;p->yaw=oldyaw;p->trailer_yaw=oldtrailer;p->road_level=oldlevel;
  if(collision_car>=0)vehicle_impact(g,&g->cars[collision_car]);
  else{p->damage=fminf(100,p->damage+ab(v)*.12f);p->v=0;p->speed_abs=0;p->lateral_v=0;}
  trailer_place(p);
 }
 p->odo+=rh_dist2(oldx,oldz,p->x,p->z)/1000.0f;
 float fuel_l=dt*(.0007f+p->engine_t*.0014f)+p->speed_abs*dt*(.00008f+p->engine_t*.00011f)*(mass/19000.0f);
 /* Single-precision PSP floats cannot represent each small cruise debit
    at a full 430-800 L tank. Retain the sub-milliliter remainder. */
 p->fuel_pending+=fuel_l*10;
 if(p->fuel_pending>=.001f){
  float debit=floorf(p->fuel_pending*1000)*.001f;
  p->fuel=fmaxf(0,p->fuel-debit);p->fuel_pending=fmaxf(0,p->fuel_pending-debit);
 }
 if(p->fuel<=0)p->fuel_pending=0;
 if(road_dist>25.0f && speed>11.0f)p->damage=fminf(100,p->damage+dt*.019f*(speed-11));
 (void)old_v;
}
/* North/south is the priority road. East/west cars wait at a 12 m
   stop line before entering a conflict, allowing the priority car to clear. */
static float junction_yield_distance(const Game *g,const Traffic *t){
 if(!(t->axis&1))return -1;
 float spacing=t->axis>=2?RH_CITY_BLOCK:RH_ROAD_SPACING;
 int gx=(int)floorf(t->x/spacing+.5f),gz=(int)floorf(t->z/spacing+.5f);
 float bx=gx*spacing,bz=gz*spacing;
 if(t->axis<2&&rh_bridge_info(gx,gz,NULL,NULL))return -1;
 float approach=(bx-t->x)*t->direction;
 if(approach< -12||approach>100)return -1;
 for(int j=0;j<g->traffic_count;j++){
  const Traffic *o=&g->cars[j];if((o->axis&1)||ab(o->x-bx)>12)continue;
  float other_approach=(bz-o->z)*o->direction;
  if(other_approach< -12||other_approach>80)continue;
  /* A car already occupying the priority lane must finish crossing rather
     than wait across that lane and create a permanent mutual blockage. */
  float front_gap=(o->x-t->x)*t->direction-traffic_half_length(t)-traffic_half_width(o);
  if(front_gap<=0)return -1;
  return fmaxf(0,approach-12);
 }
 return -1;
}
static void update_traffic(Game *g,float dt){
 for(int i=0;i<g->traffic_count;i++){
  Traffic *t=&g->cars[i];
  if(rh_dist2(t->x,t->z,g->player.x,g->player.z)>1150){spawn_traffic(g,i);continue;}
  /* Each NPC tracks a lane, brakes for a leading car and the player. */
  float speed=ab(t->speed),desired=15.5f+(t->variant*1.5f);
  float hx=sinf(t->yaw),hz=cosf(t->yaw);
  if(ab(t->x-g->player.x)<90&&ab(t->z-g->player.z)<90&&
     ab(rh_road_height(t->x,t->z,t->axis)-rh_player_height(g))<4){
   desired=avoid_body(t,desired,g->player.x,g->player.z,g->player.yaw,player_half_width(g),player_half_length(g));
   if(g->transport_mode==0&&g->job.stage==1)desired=avoid_body(t,desired,g->player.trailer_x,g->player.trailer_z,g->player.trailer_yaw,1.3f,4.3f);
  }
  if(g->parked_valid && ab(rh_road_height(t->x,t->z,t->axis)-rh_road_height(g->parked_x,g->parked_z,g->parked_level?0:1))<4){
   float tx=g->parked_x-sinf(g->parked_yaw)*RH_TRAILER_HITCH-sinf(g->parked_trailer_yaw)*RH_TRAILER_CENTER;
   float tz=g->parked_z-cosf(g->parked_yaw)*RH_TRAILER_HITCH-cosf(g->parked_trailer_yaw)*RH_TRAILER_CENTER;
   desired=avoid_body(t,desired,g->parked_x,g->parked_z,g->parked_yaw,1.25f,RH_TRUCK_HALF_LENGTH);
   desired=avoid_body(t,desired,tx,tz,g->parked_trailer_yaw,1.3f,4.3f);
  }
  for(int j=0;j<g->traffic_count;j++)if(j!=i){
   const Traffic *o=&g->cars[j];float ox=o->x-t->x,oz=o->z-t->z;
   float f=ox*hx+oz*hz,side=ab(ox*hz-oz*hx);
   if(f>0&&f<45&&side<2.8f&&cosf(o->yaw-t->yaw)>.7f)
    desired=fminf(desired,clampf((f-6)*.66f,0,desired));
  }
  float yield_distance=junction_yield_distance(g,t);
  if(yield_distance>=0)desired=fminf(desired,yield_distance*.68f);
  speed+=clampf(desired-speed,-4.2f*dt,1.3f*dt);
  if(yield_distance>=0&&dt>0)speed=fminf(speed,yield_distance/dt);
  speed=fmaxf(0,speed);
  Traffic previous=*t;
  t->speed=speed*t->direction;
  if(t->axis>=2){
   if(t->axis==2){t->z+=t->speed*dt;t->yaw=t->direction>0?0:RH_PI;}
   else{t->x+=t->speed*dt;t->yaw=t->direction>0?RH_PI/2:-RH_PI/2;}
   if(rh_cityroad_distance(t->x,t->z)>5){spawn_traffic(g,i);continue;}
  }else if(t->axis==0){
   int gx=(int)floorf(t->x/RH_ROAD_SPACING+.5f);t->z+=t->speed*dt;
   t->x=rh_road_x(gx,t->z)-t->direction*2.8f;
   t->yaw=atan2f((rh_road_x(gx,t->z+1)-rh_road_x(gx,t->z-1))*.5f,1)+(t->direction<0?RH_PI:0);
  }else{
   int gz=(int)floorf(t->z/RH_ROAD_SPACING+.5f);t->x+=t->speed*dt;
   t->z=rh_road_z(gz,t->x)+t->direction*2.8f;
   t->yaw=atan2f(1,(rh_road_z(gz,t->x+1)-rh_road_z(gz,t->x-1))*.5f)+(t->direction<0?RH_PI:0);
  }
  int hit_player=traffic_hits_player(g,t),hit_parked=traffic_hits_parked(g,t),hit_traffic=0;
  for(int j=0;j<g->traffic_count&&!hit_traffic;j++)if(j!=i){
   const Traffic *o=&g->cars[j];
   if(ab(t->x-o->x)>20||ab(t->z-o->z)>20)continue;
   if(ab(rh_road_height(t->x,t->z,t->axis)-rh_road_height(o->x,o->z,o->axis))>4)continue;
   hit_traffic=bodies_overlap(t->x,t->z,t->yaw,traffic_half_width(t),traffic_half_length(t),
                             o->x,o->z,o->yaw,traffic_half_width(o),traffic_half_length(o));
  }
  if(hit_player||hit_parked||hit_traffic){
   if(hit_player){vehicle_impact(g,t);float impact_speed=t->speed;*t=previous;t->speed=impact_speed;}
   else{*t=previous;t->speed=0;}
  }

 }
}
float rh_job_target_distance(const Game *g){
 if(g->job.stage < 0) return 0.0f;
 float x,z;int px=g->job.stage?g->job.dest_x:g->job.pickup_x;
 int pz=g->job.stage?g->job.dest_z:g->job.pickup_z;
 rh_depot(px,pz,&x,&z);return rh_dist2(g->player.x,g->player.z,x,z);
}
void rh_waypoint(const Game *g,float *x,float *z){
 const Player *p=&g->player;
 float spacing=RH_ROAD_SPACING;
 if(g->job.stage < 0){ *x=p->x; *z=p->z; return; }
 if(g->job.stage==0){rh_depot(g->job.pickup_x,g->job.pickup_z,x,z);return;}
 float gx=g->job.dest_x*spacing,gz=g->job.dest_z*spacing;
 int cx=(int)floorf(p->x/spacing+.5f),cz=(int)floorf(p->z/spacing+.5f);
 float nx=cx*spacing,nz=cz*spacing;
 // Escape company driveways by approaching the nearby public junction.
 if(rh_dist2(p->x,p->z,nx,nz)<235 &&
     (fabsf(p->x-nx)>38||fabsf(p->z-nz)>38) &&
     (fabsf(p->x-nx)>17 && fabsf(p->z-nz)>17)){
  *x=nx;*z=nz;return;
 }
 // Grid routing: travel east/west on a horizontal road, then north/south.
 if(fabsf(p->x-gx)>140){
  if(fabsf(p->z-nz)>18 && fabsf(p->x-nx)<17){*x=nx;*z=nz;}
  else {*x=p->x+clampf(gx-p->x,-180,180);*z=rh_road_z(cz,*x)+(gx>p->x?2.8f:-2.8f);}
  return;
 }
 if(fabsf(p->z-gz)>140){*z=p->z+clampf(gz-p->z,-180,180);*x=rh_road_x(g->job.dest_x,*z)-(gz>p->z?2.8f:-2.8f);return;}
 rh_depot(g->job.dest_x,g->job.dest_z,x,z);
}
static void interact(Game *g){
 if(g->transport_mode==3)return;
 Player *p=&g->player;int cx=(int)floorf(p->x/RH_ROAD_SPACING+.5f);
 int cz=(int)floorf(p->z/RH_ROAD_SPACING+.5f);float x,z;
 /* Заправка / ремонт */
 rh_station(cx,cz,&x,&z);
 if(rh_dist2(p->x,p->z,x,z)<27 && p->speed_abs<2.0f){
  float refill=RH_TRUCKS[p->model].tank_l-p->fuel;float affordable=fminf(refill,g->bank/2.15f);
  if(affordable>0){p->fuel+=affordable;g->bank-=affordable*2.15f;g->last_notice=3;g->notice_ticks=180;}
  if(p->damage>0 && g->bank>0){float repair=fminf(p->damage,g->bank/43.0f);
   g->bank-=repair*43.0f;p->damage-=repair;g->last_notice=6;g->notice_ticks=150;}
  return;
 }
 if(g->transport_mode==3 || (g->transport_mode!=0 && g->parked_valid))return;

 int tx=g->job.stage?g->job.dest_x:g->job.pickup_x;
 int tz=g->job.stage?g->job.dest_z:g->job.pickup_z;
 rh_depot(tx,tz,&x,&z);
 if(rh_dist2(p->x,p->z,x,z)<32 && p->speed_abs<1.4f){
  if(g->job.stage==0){
   /* === ПРИЦЕПЛЕНИЕ ПРИЦЕПА === */
   g->job.stage=1;
   p->trailer_yaw=p->yaw;
   trailer_place(p);
   g->parked_valid=0;
   g->last_notice=1; /* ГРУЗ ЗАГРУЖЕН */
   g->notice_ticks=220;
  } else if(g->job.stage==1){
   /* === ОТКРЫТЬ МЕНЮ СДАЧИ (с бонусами) === */
   g->state = ST_UNLOAD;
   /* auto-select: reverse approach -> option 2 (2500), forward -> 1 (1000) */
   if(g->approach_dot < -0.35f || g->player.reverse)
    g->unload_sel = 2;
   else if(g->approach_dot > 0.35f)
    g->unload_sel = 1;
   else
    g->unload_sel = 0;
  }
 }
}
static void start_service(Game *g){
 Player *p=&g->player;
 if(g->service_phase||p->fuel>=.01f||p->speed_abs>=.5f)return;
 g->service_phase=1;g->service_timer=0;g->service_yaw=p->yaw;
 g->service_x=p->x-sinf(p->yaw)*200-cosf(p->yaw)*(p->road_level?-4.5f:4.5f);
 g->service_z=p->z-cosf(p->yaw)*200+sinf(p->yaw)*(p->road_level?-4.5f:4.5f);
 p->fuel=0;p->fuel_pending=0;
}
static void update_service(Game *g,float dt){
 while(g->service_phase&&dt>0){
  int phase=g->service_phase;float duration=phase==1?50:phase==2?30:8;
  float step=fminf(dt,fmaxf(0,duration-g->service_timer));
  float speed=phase==1?4:phase==3?15:0;
  g->service_x+=sinf(g->service_yaw)*speed*step;
  g->service_z+=cosf(g->service_yaw)*speed*step;
  if(g->player.road_level){
   int gx=(int)floorf(g->player.x/RH_ROAD_SPACING+.5f);
   g->service_x=rh_road_x(gx,g->service_z)+(cosf(g->service_yaw)>=0?1.7f:-1.7f);
  }
  g->service_timer+=step;dt-=step;
  if(phase==2){g->player.v=0;g->player.speed_abs=0;g->player.lateral_v=0;g->player.engine_t=0;}
  /* Float elapsed accumulation stays within one millisecond of the boundary. */
  if(g->service_timer>=duration-.001f){
   if(phase==2){g->player.fuel=fminf(80,RH_TRUCKS[g->player.model].tank_l);g->player.fuel_pending=0;}
   g->service_phase=phase==3?0:phase+1;g->service_timer=0;
   if(g->service_phase==2){g->player.v=0;g->player.speed_abs=0;g->player.lateral_v=0;}
  }else break;
 }
}
static int next_level(float ox,float oz,float x,float z,int level){
 int gx,gz,ogx,ogz;int over=bridge_at(x,z,&gx,&gz);
 if(!level&&over&&ab(z-gz*RH_ROAD_SPACING)>500&&ab(rh_bridge_deck(gx,gz,z)-rh_height(x,z))<.8f)return 1;
 if(level&&!over&&bridge_at(ox,oz,&ogx,&ogz)&&ab(rh_bridge_deck(ogx,ogz,oz)-rh_height(ox,oz))<.8f)return 0;
 return level;
}
/* Light vehicles share the same world coordinates while retaining the truck's
   economy and cargo state. Their decorative trailers follow the hitch. */
static int light_hits_parked(const Game *g,float x,float z,float yaw){
 if(!g->parked_valid || ab(rh_player_height(g)-rh_road_height(g->parked_x,g->parked_z,g->parked_level?0:1))>4)return 0;
 float tx=g->parked_x-sinf(g->parked_yaw)*RH_TRAILER_HITCH-sinf(g->parked_trailer_yaw)*RH_TRAILER_CENTER;
 float tz=g->parked_z-cosf(g->parked_yaw)*RH_TRAILER_HITCH-cosf(g->parked_trailer_yaw)*RH_TRAILER_CENTER;
 return bodies_overlap(x,z,yaw,player_half_width(g),player_half_length(g),g->parked_x,g->parked_z,g->parked_yaw,1.25f,RH_TRUCK_HALF_LENGTH)||
        bodies_overlap(x,z,yaw,player_half_width(g),player_half_length(g),tx,tz,g->parked_trailer_yaw,1.3f,4.3f);
}
static void light_step(Game *g,Controls in,float dt){
 Player *p=&g->player;float ox=p->x,oz=p->z,oy=p->yaw,ot=p->trailer_yaw;int oldlevel=p->road_level;
 const float limits[4]={25,35,52,8}, accel[4]={0,4.5f,7.5f,2.5f};
 float target=clampf(in.throttle,0,1)*limits[g->transport_mode];
 if(in.brake>.1f)target=-clampf(in.brake,0,1)*4;
 if(g->transport_mode!=3&&p->fuel<.01f)target=0;
 if(in.handbrake)target=0;
 float delta=clampf(target-p->v,-dt*8,dt*accel[g->transport_mode]);
 p->v+=delta;p->speed_abs=ab(p->v);
 p->yaw=wrapped(p->yaw+clampf(in.steer,-1,1)*p->v*dt/(g->transport_mode==3?2.5f:4.5f)/(1+p->speed_abs*.08f));
 p->x+=sinf(p->yaw)*p->v*dt;p->z+=cosf(p->yaw)*p->v*dt;
 p->trailer_yaw=wrapped(p->trailer_yaw+wrapped(p->yaw-p->trailer_yaw)*fminf(1,ab(p->v)*dt*.35f));
 trailer_place(p);
 p->road_level=next_level(ox,oz,p->x,p->z,oldlevel);
 if(body_blocked(p->x,p->z,p->yaw,g->transport_mode==3?.4f:1.0f,g->transport_mode==3?1.0f:2.3f,p->road_level)||player_hits_traffic(g)>=0||
    (light_hits_parked(g,p->x,p->z,p->yaw)&&(!light_hits_parked(g,ox,oz,oy)||rh_dist2(p->x,p->z,g->parked_x,g->parked_z)<rh_dist2(ox,oz,g->parked_x,g->parked_z)))){
  p->x=ox;p->z=oz;p->yaw=oy;p->trailer_yaw=ot;p->road_level=oldlevel;p->v=p->speed_abs=0;trailer_place(p);
 }
 float distance=rh_dist2(ox,oz,p->x,p->z);p->odo+=distance/1000.0f;
 if(g->transport_mode!=3&&p->fuel>0){p->fuel_pending+=distance*.00012f+dt*.001f;
  if(p->fuel_pending>=.001f){float debit=floorf(p->fuel_pending*1000)*.001f;p->fuel=fmaxf(0,p->fuel-debit);p->fuel_pending-=debit;}
  if(p->fuel<=0)p->fuel_pending=0;
 }
}
static void person_step(Game *g,Controls in,float dt){
 float x=g->person_x,z=g->person_z;int oldlevel=g->person_level;
 g->person_yaw=wrapped(g->person_yaw+clampf(in.steer,-1,1)*2.4f*dt);
 float speed=(clampf(in.throttle,0,1)-clampf(in.brake,0,1))*3.5f;
 g->person_x+=sinf(g->person_yaw)*speed*dt;g->person_z+=cosf(g->person_yaw)*speed*dt;
 g->person_level=next_level(x,z,g->person_x,g->person_z,oldlevel);
 if(body_blocked(g->person_x,g->person_z,g->person_yaw,.3f,.3f,g->person_level)){g->person_x=x;g->person_z=z;g->person_level=oldlevel;}
}
void rh_step(Game *g,Controls in,float dt){
 /* ===== STATE MACHINE ===== */
 if(g->state == ST_MENU){
  rh_enter_garage(g);
  return;
 }

 if(g->state == ST_GARAGE){
  /* авто-вращение + ручной осмотр стиком */
  float orbit = in.cam_orbit;
  if(orbit > -0.05f && orbit < 0.05f)
   g->garage_cam_yaw += 0.40f * dt; /* медленный автоповорот */
  else
   g->garage_cam_yaw += orbit * 2.2f * dt;
  while(g->garage_cam_yaw > RH_PI*2) g->garage_cam_yaw -= RH_PI*2;
  while(g->garage_cam_yaw < 0) g->garage_cam_yaw += RH_PI*2;
  g->garage_cam_dist = clampf(g->garage_cam_dist + in.cam_zoom * 10.0f * dt, 8.0f, 22.0f);
  g->garage_cam_height = clampf(4.0f + (14.0f - g->garage_cam_dist) * 0.15f, 2.5f, 7.0f);

  if(in.action){ /* CIRCLE -> computer */
   g->state = ST_COMPUTER;
   g->computer_sel = 0;
   return;
  }
  /* выезд только после взятия заказа в компьютере */
  return;
 }

 if(g->state == ST_COMPUTER){
  if(in.menu_up)   g->computer_sel = (g->computer_sel + 2) % 3;
  if(in.menu_down) g->computer_sel = (g->computer_sel + 1) % 3;
  if(in.menu_accept){
  int from = ((g->home_city % 4) + 4) % 4;
  int to = ((from + 1 + g->computer_sel) % 4 + 4) % 4;
   Job *j = &g->job;
   j->pickup_x = CITY_GX[from];
   j->pickup_z = CITY_GZ[from];
   j->dest_x = CITY_GX[to];
   j->dest_z = CITY_GZ[to];
   j->cargo_tons = 10 + g->computer_sel * 4 + rndi(g, 6);
   j->type = g->computer_sel;
   j->stage = 0;
   j->distance_km = (ab((float)(j->dest_x - j->pickup_x)) + ab((float)(j->dest_z - j->pickup_z))) * RH_ROAD_SPACING / 1000.0f;
   if(j->distance_km < 5) j->distance_km = 8 + g->computer_sel * 3;
   j->payout = (900.0f + 40.0f * j->cargo_tons) * j->distance_km;
   rh_leave_garage(g);
   return;
  }
  if(in.action){ /* circle back */
   g->state = ST_GARAGE;
   return;
  }
  return;
 }

 if(g->state == ST_UNLOAD){
  if(in.menu_up)   g->unload_sel = (g->unload_sel + 2) % 3;
  if(in.menu_down) g->unload_sel = (g->unload_sel + 1) % 3;
  if(in.menu_accept){
   float bonus = (g->unload_sel == 0) ? 0.0f : (g->unload_sel == 1) ? 1000.0f : 2500.0f;
   g->bank += g->job.payout + bonus;
   g->completed++;
   g->last_notice = 2; /* РЕЙС ВЫПОЛНЕН */
   g->notice_ticks = 240;
   /* отцепить прицеп */
   int dest_x = g->job.dest_x, dest_z = g->job.dest_z;
   g->job.stage = -1;
   g->player.trailer_yaw = g->player.yaw;
   trailer_place(&g->player);
   /* следующий возможный заказ из текущего города */
   init_job(g, dest_x, dest_z);
   g->job.stage = -1; /* свободная езда, заказ не активен */
   g->state = ST_PLAY;
   return;
  }
  return;
 }


 if(g->state == ST_RADIO){
  if(in.menu_up)   g->radio_station = (g->radio_station + 3) % 4;
  if(in.menu_down) g->radio_station = (g->radio_station + 1) % 4;
  if(in.menu_left || in.action){ g->state = ST_PLAY; g->radio_open = 0; return; }
  return;
 }
 if(g->state == ST_PHONE){
  if(in.menu_up)   g->phone_sel = (g->phone_sel + 2) % 3;
  if(in.menu_down) g->phone_sel = (g->phone_sel + 1) % 3;
  if(in.menu_accept){
   init_job(g, (int)(g->player.x/RH_ROAD_SPACING), (int)(g->player.z/RH_ROAD_SPACING));
   g->job.stage = 0;
   g->state = ST_PLAY;
   return;
  }
  if(in.action){ g->state = ST_PLAY; return; }
  return;
 }

 /* не сбрасываем MENU/GARAGE/COMPUTER/PHONE/UNLOAD/RADIO */


 if(g->state==ST_PLAY && !g->paused && !g->map_open && in.menu_up && g->player.speed_abs >= 0.5f){
  g->state = ST_RADIO; g->radio_open = 1; return;
 }
 if(in.pause)g->paused=!g->paused;
 if(in.toggle_camera)g->player.camera=(g->player.camera+1)%2;
 if(in.map)g->map_open=!g->map_open;
 if(g->paused){
  if(in.menu_up||in.menu_down)g->settings_row=1-g->settings_row;
  int change=(in.menu_right||in.menu_accept?1:0)-(in.menu_left?1:0);
  if(change){
   int *mode=g->settings_row?&g->time_mode:&g->weather_mode;
   *mode=(*mode+change+5)%5;
   if(g->weather_mode)g->weather=g->weather_mode-1;
   const float hours[5]={0,12,17.6f,0,6.4f};
   if(g->time_mode)g->clock_hours=hours[g->time_mode];
  }
  return;
 }
 if(!isfinite(dt)||dt<0)dt=0;
 if(dt>.07f)dt=.07f;
 if(!g->map_open){
  Player *p=&g->player;
  if(in.menu_up && (!g->on_foot ? p->speed_abs<.5f : rh_dist2(g->person_x,g->person_z,p->x,p->z)<6)){
   if(!g->on_foot){g->person_x=p->x+cosf(p->yaw)*3;g->person_z=p->z-sinf(p->yaw)*3;g->person_yaw=p->yaw;g->person_level=p->road_level;p->v=p->speed_abs=0;g->on_foot=1;}
   else if(g->person_level==p->road_level)g->on_foot=0;
  }
  if(in.menu_left && p->speed_abs<.5f && !(g->job.stage==1 && g->transport_mode!=0 && !g->parked_valid)){
   if(g->transport_mode==0&&g->job.stage==1){g->parked_valid=1;g->parked_x=p->x;g->parked_z=p->z;g->parked_yaw=p->yaw;g->parked_trailer_yaw=p->trailer_yaw;g->parked_level=p->road_level;}
   g->transport_mode=(g->transport_mode+1)%4;p->v=p->speed_abs=0;
   if(g->transport_mode==0&&g->parked_valid){rh_set_player(g,g->parked_x,g->parked_z,g->parked_yaw);p->road_level=g->parked_level;p->trailer_yaw=g->parked_trailer_yaw;g->parked_valid=0;}else p->trailer_yaw=p->yaw;
   trailer_place(p);
  }
  if(in.menu_right && g->on_foot){
   /* Search locally for a free place rather than placing the vehicle inside scenery. */
   for(int i=0;i<16;i++){float a=i*RH_PI/8, x=g->person_x+cosf(a)*5,z=g->person_z+sinf(a)*5;
    if(!body_blocked(x,z,p->yaw,1.25f,3.5f,g->person_level)){Player old=*p;rh_set_player(g,x,z,p->yaw);p->road_level=g->person_level;if(player_hits_traffic(g)<0)break;*p=old;}
   }
  }
 }
 if(g->map_open&&in.action&&!g->on_foot&&g->transport_mode!=3)start_service(g);
 update_service(g,dt);
 /* The order/map screen is an overlay: the world and assistance continue.
    Driving commands are held at the brakes until the player closes it. */
 if(g->map_open){memset(&in,0,sizeof in);in.brake=1;in.handbrake=1;}
 if(in.swap_truck&&g->service_phase!=2){
  int cx=(int)floorf(g->player.x/RH_ROAD_SPACING+.5f);
  int cz=(int)floorf(g->player.z/RH_ROAD_SPACING+.5f);float x,z;
  rh_depot(cx,cz,&x,&z);
  if(rh_dist2(g->player.x,g->player.z,x,z)<32 && g->player.speed_abs<1.0f){
   int model=(g->player.model+1)%3;
   float cost=(g->owned_trucks&(1u<<model))?0.0f:RH_TRUCKS[model].price;
   if(g->bank>=cost){g->bank-=cost;g->owned_trucks|=(1u<<model);
    float f=g->player.fuel/RH_TRUCKS[g->player.model].tank_l;
    g->player.model=model;g->player.fuel=f*RH_TRUCKS[model].tank_l;
    g->last_notice=4;g->notice_ticks=200;
   } else {g->last_notice=5;g->notice_ticks=200;}
  }
 }

 /* Track approach direction to depot for reverse-parking bonus */
 if(g->state==ST_PLAY && g->job.stage==1){
  float dx,dz,tx,tz;
  rh_depot(g->job.dest_x,g->job.dest_z,&tx,&tz);
  dx = tx - g->player.x; dz = tz - g->player.z;
  float len = sqrtf(dx*dx+dz*dz);
  if(len > 1.0f && len < 80.0f){
   dx/=len; dz/=len;
   float fx = sinf(g->player.yaw), fz = cosf(g->player.yaw);
   /* positive = facing target, negative = reversing toward target */
   g->approach_dot = fx*dx + fz*dz;
  }
 }
 /* Phone on the road (ТЗ): Circle when free-roaming */
 if(in.action && g->state==ST_PLAY && !g->on_foot && g->service_phase!=2){
  float tx,tz;
  rh_depot(g->job.stage?g->job.dest_x:g->job.pickup_x,
           g->job.stage?g->job.dest_z:g->job.pickup_z,&tx,&tz);
  float bay = rh_dist2(g->player.x,g->player.z,tx,tz);
  if(g->job.stage==1 && bay < 28.0f && g->player.speed_abs < 1.5f){
   /* arrived at unload zone */
   g->state = ST_UNLOAD;
   /* auto-select: reverse approach -> option 2 (2500), forward -> 1 (1000) */
   if(g->approach_dot < -0.35f || g->player.reverse)
    g->unload_sel = 2;
   else if(g->approach_dot > 0.35f)
    g->unload_sel = 1;
   else
    g->unload_sel = 0;
  } else if(bay > 40.0f && g->job.stage != 0){
   /* free drive phone */
   g->state = ST_PHONE;
   g->phone_sel = 0;
  } else {
   interact(g);
  }
 } else if(in.action&&g->service_phase!=2&&!g->on_foot){
  interact(g);
 }
 if(g->transport_mode==0&&!g->on_foot&&g->player.fuel<.01f)in.throttle=0;
 // 2 substeps preserve consistent handling near 30 FPS.
 if(g->service_phase!=2){
  if(g->on_foot){if(!g->map_open)person_step(g,in,dt);}
  else if(g->transport_mode)light_step(g,in,dt);
  else {physics_step(g,in,dt*.5f);in.reverse_request=0;physics_step(g,in,dt*.5f);}
 }
 /* Only repeated active driving against an obstruction triggers recovery. */
 if(!g->on_foot&&g->transport_mode==0&&!g->map_open&&g->service_phase==0&&g->player.fuel>.01f&&!in.handbrake&&
    ((g->player.reverse&&in.brake>.7f&&in.throttle<.1f)||(!g->player.reverse&&in.throttle>.7f&&in.brake<.1f))&&g->player.speed_abs<.15f){
  if(g->traffic_t<0||g->traffic_t>20)g->traffic_t=0;
  g->traffic_t+=dt;if(g->traffic_t>=8){recover_stuck(g);g->traffic_t=0;}
 }else g->traffic_t=0;
 update_traffic(g,dt);g->sim_t+=dt;if(!g->time_mode)g->clock_hours+=dt/300.0f;
 g->current_city = rh_nearest_city(g->player.x, g->player.z);
 if(g->weather_mode)g->weather=g->weather_mode-1;
 else {
  /* Migrate an old 50–150 km countdown without changing the weather immediately. */
  if(g->weather_next_odo>g->player.odo+10)g->weather_next_odo=g->player.odo+3+rndi(g,8);
  if(g->player.odo>=g->weather_next_odo){g->weather=(g->weather+1+rndi(g,3))%4;g->weather_next_odo=g->player.odo+3+rndi(g,8);}
 }
 if(g->clock_hours>=24)g->clock_hours-=24;
 if(g->notice_ticks>0)g->notice_ticks--;
}

typedef struct {uint32_t magic,version,checksum;Game g;} DiskSave;
static uint32_t checksum(const unsigned char *b,size_t n){uint32_t h=2166136261u;for(size_t i=0;i<n;i++){h^=b[i];h*=16777619u;}return h;}
/* Two-generation write strategy on FAT: a failed or interrupted write
   must not overwrite the most recent complete SAVE.DAT. No flash access. */
int rh_save(const Game *g,const char *path){
 if(!g||!path||strlen(path)>230)return 0;
 DiskSave s;memset(&s,0,sizeof s);s.magic=0x52485356u;s.version=RH_SAVE_VERSION;
 s.g=*g;s.checksum=checksum((const unsigned char*)&s.g,sizeof s.g);
 char temp[256],backup[256];
 snprintf(temp,sizeof temp,"%s.NEW",path);
 snprintf(backup,sizeof backup,"%s.BAK",path);
 FILE *f=fopen(temp,"wb");if(!f)return 0;
 int okay=(fwrite(&s,1,sizeof s,f)==sizeof s);
 if(fflush(f)!=0)okay=0;
 if(fclose(f)!=0)okay=0;
 if(!okay){remove(temp);return 0;}
 /* The backup itself is never touched if a save could not be written. */
 remove(backup);
 int old_moved=(rename(path,backup)==0);
 if(rename(temp,path)!=0){
  if(old_moved)rename(backup,path);
  remove(temp);return 0;
 }
 return 1;
}
/* Exact v5 disk layout: new fields must never reinterpret old player offsets. */
typedef struct { float x,z,yaw,v,speed_abs,rpm,fuel,odo,damage,trailer_yaw;
 float engine_t,gearbox_t;int gear,model,camera,reverse; } PlayerV5;
typedef struct { PlayerV5 player;Job job;Traffic cars[RH_MAX_TRAFFIC];
 int traffic_count,paused,map_open,completed,last_notice,notice_ticks;unsigned int owned_trucks;
 float bank,drive_time,traffic_t,sim_t,clock_hours;uint32_t rng;} GameV5;
typedef struct {float x,z,yaw,v,speed_abs,rpm,fuel,odo,damage,trailer_yaw,engine_t,gearbox_t;
 int gear,model,camera,reverse;float trailer_x,trailer_z,lateral_v,fuel_pending;} PlayerV6;
typedef struct {PlayerV6 player;Job job;Traffic cars[RH_MAX_TRAFFIC];
 int traffic_count,paused,map_open,completed,last_notice,notice_ticks;unsigned int owned_trucks;
 float bank,drive_time,traffic_t,sim_t,clock_hours;uint32_t rng;
 int weather,weather_mode,time_mode,settings_row;float weather_next_odo;
 int service_phase;float service_timer,service_x,service_z,service_yaw;} GameV6;
int rh_load(Game *g,const char *path){
 if(!g||!path)return 0;
 DiskSave s;memset(&s,0,sizeof s);FILE *f=fopen(path,"rb");if(!f)return 0;
 uint32_t head[3];if(fread(head,1,sizeof head,f)!=sizeof head||head[0]!=0x52485356u){fclose(f);return 0;}
 if(head[1]==5){
  GameV5 old;
  if(fread(&old,1,sizeof old,f)!=sizeof old||fgetc(f)!=EOF||head[2]!=checksum((unsigned char*)&old,sizeof old)){fclose(f);return 0;}
  rh_new_game(&s.g);
  memcpy(&s.g.player,&old.player,sizeof old.player);
  s.g.job=old.job;memcpy(s.g.cars,old.cars,sizeof old.cars);
  s.g.traffic_count=old.traffic_count;s.g.paused=old.paused;s.g.map_open=old.map_open;
  s.g.completed=old.completed;s.g.last_notice=old.last_notice;s.g.notice_ticks=old.notice_ticks;
  s.g.owned_trucks=old.owned_trucks;s.g.bank=old.bank;s.g.drive_time=old.drive_time;
  s.g.traffic_t=old.traffic_t;s.g.sim_t=old.sim_t;s.g.clock_hours=old.clock_hours;s.g.rng=old.rng;
  s.g.weather_next_odo=old.player.odo+3+rndi(&s.g,8);trailer_place(&s.g.player);
 }else if(head[1]==6){
  GameV6 old;
  if(fread(&old,1,sizeof old,f)!=sizeof old||fgetc(f)!=EOF||head[2]!=checksum((unsigned char*)&old,sizeof old)){fclose(f);return 0;}
  memset(&s.g,0,sizeof s.g);memcpy(&s.g.player,&old.player,sizeof old.player);
  memcpy(&s.g.job,&old.job,sizeof old-sizeof old.player);
  int gx,gz;s.g.player.road_level=ab(cosf(s.g.player.yaw))>=.7071f&&bridge_at(s.g.player.x,s.g.player.z,&gx,&gz);
 }else if(head[1]==7){
  size_t old_size=offsetof(Game,transport_mode);
  if(fread(&s.g,1,old_size,f)!=old_size||fgetc(f)!=EOF||head[2]!=checksum((unsigned char*)&s.g,old_size)){fclose(f);return 0;}
 }else if(head[1]==8){
  size_t old_size=offsetof(Game,person_level);
  if(fread(&s.g,1,old_size,f)!=old_size||fgetc(f)!=EOF||head[2]!=checksum((unsigned char*)&s.g,old_size)){fclose(f);return 0;}
  s.g.person_level=s.g.player.road_level;
  if(s.g.transport_mode&&s.g.job.stage==1){s.g.parked_valid=1;s.g.parked_x=s.g.player.x;s.g.parked_z=s.g.player.z;s.g.parked_yaw=s.g.player.yaw;s.g.parked_level=s.g.player.road_level;s.g.parked_trailer_yaw=s.g.player.trailer_yaw;}
 }else if(head[1]==RH_SAVE_VERSION){
  if(fread(&s.g,1,sizeof s.g,f)!=sizeof s.g||fgetc(f)!=EOF||head[2]!=checksum((unsigned char*)&s.g,sizeof s.g)){fclose(f);return 0;}
 }else{fclose(f);return 0;}
 fclose(f);
 if(s.g.person_level<0||s.g.person_level>1||s.g.parked_valid<0||s.g.parked_valid>1||s.g.parked_level<0||s.g.parked_level>1||!isfinite(s.g.parked_x)||!isfinite(s.g.parked_z)||!isfinite(s.g.parked_yaw)||!isfinite(s.g.parked_trailer_yaw))return 0;
 if(s.g.transport_mode<0||s.g.transport_mode>3||s.g.on_foot<0||s.g.on_foot>1||!isfinite(s.g.person_x)||!isfinite(s.g.person_z)||!isfinite(s.g.person_yaw))return 0;
 if(s.g.player.model<0||s.g.player.model>2||s.g.player.gear<1||s.g.player.gear>8||
    s.g.owned_trucks>7u||s.g.traffic_count<0||s.g.traffic_count>RH_MAX_TRAFFIC||
    s.g.job.stage<0||s.g.job.stage>1||s.g.job.cargo_tons<0||s.g.job.cargo_tons>60||
    s.g.player.camera<0||s.g.player.camera>1||s.g.player.reverse<0||s.g.player.reverse>1||s.g.player.road_level<0||s.g.player.road_level>1||
    s.g.weather<0||s.g.weather>3||s.g.weather_mode<0||s.g.weather_mode>4||
    s.g.time_mode<0||s.g.time_mode>4||s.g.settings_row<0||s.g.settings_row>1||s.g.service_phase<0||s.g.service_phase>3)return 0;
 if(!isfinite(s.g.service_timer)||s.g.service_timer<0||s.g.service_timer>50.01f||
    !isfinite(s.g.service_x)||!isfinite(s.g.service_z)||!isfinite(s.g.service_yaw))return 0;
 if(!isfinite(s.g.player.x)||!isfinite(s.g.player.z)||!isfinite(s.g.player.v)||
    !isfinite(s.g.player.fuel)||!isfinite(s.g.player.damage)||!isfinite(s.g.bank)||
    !isfinite(s.g.sim_t)||!isfinite(s.g.player.yaw)||!isfinite(s.g.player.trailer_yaw)||
    !isfinite(s.g.player.trailer_x)||!isfinite(s.g.player.trailer_z)||!isfinite(s.g.player.lateral_v)||
    !isfinite(s.g.player.fuel_pending)||s.g.player.fuel_pending<0||s.g.player.fuel_pending>.0011f||
    !isfinite(s.g.player.odo)||!isfinite(s.g.weather_next_odo)||!isfinite(s.g.clock_hours)||
    !isfinite(s.g.player.rpm)||!isfinite(s.g.player.engine_t)||!isfinite(s.g.player.gearbox_t)||
    fabsf(s.g.player.x)>10000000||fabsf(s.g.player.z)>10000000||
    s.g.bank<0||s.g.bank>1e9f||s.g.player.fuel<0||s.g.player.odo<0||
    s.g.clock_hours<0||s.g.clock_hours>=24||
    s.g.player.fuel>RH_TRUCKS[s.g.player.model].tank_l+1||
    s.g.player.damage<0||s.g.player.damage>100)return 0;
 for(int i=0;i<s.g.traffic_count;i++){
  Traffic *t=&s.g.cars[i];if(!isfinite(t->x)||!isfinite(t->z)||!isfinite(t->yaw)||!isfinite(t->speed)||
   t->axis<0||t->axis>3||(t->direction!=1&&t->direction!=-1)||t->variant<0||t->variant>3)return 0;
 }
 *g=s.g;return 1;
}
