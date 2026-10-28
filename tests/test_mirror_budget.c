#include "sim.h"
#include "render_psp.h"
#include <stdio.h>
extern int mock_2d,mock_3d;
int main(void){Game g;rh_new_game(&g);rh_render_init();rh_set_player(&g,0,90,0);g.player.camera=1;g.job.stage=1;g.traffic_count=0;rh_render_frame(&g,0);int baseline=mock_2d;g.traffic_count=RH_MAX_TRAFFIC;for(int i=0;i<RH_MAX_TRAFFIC;i++){g.cars[i].x=(i%3-1)*2;g.cars[i].z=85-i*3;g.cars[i].yaw=0;}mock_2d=mock_3d=0;rh_render_frame(&g,0);printf("baseline %d traffic %d delta %d\n",baseline,mock_2d,mock_2d-baseline);return mock_2d-baseline>180;}
