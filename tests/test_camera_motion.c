#include "sim.h"
#include "render_psp.h"
#include "psp_mock/mock_common.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
extern ScePspFVector3 mock_camera_eye,mock_camera_forward;
int main(void){
 Game g;rh_new_game(&g);rh_render_init();g.traffic_count=0;g.player.camera=0;g.sim_t=1;
 rh_set_player(&g,0,90,0);rh_render_frame(&g,0);float prev=mock_camera_eye.x;
 g.player.yaw=.7f;g.sim_t+=1.f/60;rh_render_frame(&g,0);
 float dx=fabsf(mock_camera_eye.x-prev);assert(dx>0.05f&&dx<3.0f);
 for(int i=0;i<120;i++){g.sim_t+=1.f/60;rh_render_frame(&g,0);}
 assert(fabsf(mock_camera_eye.x+17*sinf(.7f))<.3f);
 g.player.camera=1;g.sim_t+=1.f/60;rh_render_frame(&g,0);
 float x=mock_camera_eye.x,z=mock_camera_eye.z;
 for(int i=0;i<20;i++){g.sim_t+=1.f/60;rh_render_frame(&g,1);}
 assert(fabsf(mock_camera_eye.x-x)<.001f&&fabsf(mock_camera_eye.z-z)<.001f);
 rh_render_end();puts("PASS: exterior camera follows smoothly; cabin eye remains fixed when looking aside");
}
