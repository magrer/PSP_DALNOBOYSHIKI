#include "sim.h"
#include <math.h>
#include <assert.h>
#include <stdio.h>
int main(){for(int tow=0;tow<2;tow++){Game g;rh_new_game(&g);g.traffic_count=0;g.traffic_t=-1e9;g.job.stage=tow;float x=-600;rh_set_player(&g,x,rh_road_z(4,x),RH_PI/2);Controls c={0};for(int i=0;i<30000&&g.player.x<600;i++){float look=22+g.player.speed_abs*2,tx=g.player.x+look,tz=rh_road_z(4,tx);float e=atan2f(look,tz-g.player.z)-g.player.yaw;while(e>RH_PI)e-=2*RH_PI;while(e< -RH_PI)e+=2*RH_PI;c.steer=fmaxf(-1,fminf(1,e*1.8f));c.throttle=fmaxf(0,fminf(1,(12-g.player.v)*.45f));c.brake=fmaxf(0,fminf(1,(g.player.v-13)*.4f));rh_step(&g,c,1.f/60);}assert(g.player.x>=600&&g.player.road_level==0&&g.player.damage<.01f);printf("tow %d x %.2f z %.2f level %d damage %.2f\n",tow,g.player.x,g.player.z,g.player.road_level,g.player.damage);}}
