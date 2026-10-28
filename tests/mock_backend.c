#include "psp_mock/mock_common.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
int mock_frames=0,mock_3d=0,mock_2d=0;
int mock_sun_vertices,mock_moon_vertices,mock_star_vertices;
static int draw_number;
static int depth_enabled,clip_enabled;
static ScePspFVector3 eye,forward;
ScePspFVector3 mock_camera_eye,mock_camera_forward;
typedef struct {uint32_t c;float x,y,z;} MockVertex;
typedef struct {float u,v;uint32_t c;float x,y,z;} MockTexVertex;
void sceGuDrawArray(int type,int format,int count,const void* index,const void *vertices){
 (void)type;(void)index;
 const unsigned char *raw=vertices;
 if(count<0||count>55000){fprintf(stderr,"invalid batch: %d\n",count);abort();}
 if(format & GU_TRANSFORM_2D){
  if(depth_enabled||clip_enabled){fprintf(stderr,"HUD still uses 3D clipping/depth\n");abort();}
  mock_2d+=count;
 }else mock_3d+=count;
 for(int i=0;i<count;i++){
  const MockVertex *v;
  if((format&3)==GU_TEXTURE_32BITF){
   const MockTexVertex *t=(const MockTexVertex*)raw+i;
   if(!isfinite(t->u)||!isfinite(t->v)){fprintf(stderr,"invalid clipped UV\n");abort();}
   v=(const MockVertex*)&t->c;
  }else v=(const MockVertex*)raw+i;
  if(draw_number==0&&(format&GU_TRANSFORM_2D)){
   if(v->c==0xffa5f0ffu)mock_sun_vertices++;
   if(v->c==0xffe8e9e0u)mock_moon_vertices++;
   if(v->c==0xffc7a993u||v->c==0xffded6cdu)mock_star_vertices++;
  }
  if(!(format&GU_TRANSFORM_2D)){
   float near=(v->x-eye.x)*forward.x+(v->y-eye.y)*forward.y+(v->z-eye.z)*forward.z;
   if(near<.79f){fprintf(stderr,"unclipped camera-plane triangle: %.4f\n",near);abort();}
   float len=sqrtf(forward.x*forward.x+forward.z*forward.z);
   float rx=-forward.z/len,rz=forward.x/len;
   float ux=-rz*forward.y,uy=rz*forward.x-rx*forward.z,uz=rx*forward.y;
   float dx=v->x-eye.x,dy=v->y-eye.y,dz=v->z-eye.z;
   float horizontal=dx*rx+dz*rz,vertical=dx*ux+dy*uy+dz*uz;
   if(fabsf(horizontal)>near*1.206f+.01f||fabsf(vertical)>near*.684f+.01f){
    fprintf(stderr,"triangle outside PSP screen guard band\n");abort();
   }
  }
  if(!isfinite(v->x)||!isfinite(v->y)||!isfinite(v->z)){
   fprintf(stderr,"NaN/Inf vertex %d\n",i);abort();
  }
 }
 draw_number++;
}
void sceGumDrawArray(int type,int format,int count,const void* index,const void *vertices){
 sceGuDrawArray(type,format,count,index,vertices);
}
#define IMPL_VOID(name,signature) void name signature {}
IMPL_VOID(sceGuInit,(void))
void sceGuStart(int x,void*y){(void)x;(void)y;draw_number=0;mock_sun_vertices=mock_moon_vertices=mock_star_vertices=0;}
IMPL_VOID(sceGuDrawBuffer,(int x,void*y,int z))
IMPL_VOID(sceGuDispBuffer,(int x,int y,void *p,int z))
IMPL_VOID(sceGuDepthBuffer,(void *p,int w))
IMPL_VOID(sceGuOffset,(int a,int b))
IMPL_VOID(sceGuViewport,(int a,int b,int c,int d))
IMPL_VOID(sceGuDepthRange,(int a,int b))
IMPL_VOID(sceGuScissor,(int a,int b,int c,int d))
void sceGuEnable(int a){if(a==GU_DEPTH_TEST)depth_enabled=1;if(a==GU_CLIP_PLANES)clip_enabled=1;}
void sceGuDisable(int a){if(a==GU_DEPTH_TEST)depth_enabled=0;if(a==GU_CLIP_PLANES)clip_enabled=0;}
IMPL_VOID(sceGuDepthFunc,(int a))
IMPL_VOID(sceGuShadeModel,(int a))
IMPL_VOID(sceGuFinish,(void))
IMPL_VOID(sceGuSync,(int a,int b))
IMPL_VOID(sceGuDisplay,(int a))
IMPL_VOID(sceGuClearColor,(unsigned a))
IMPL_VOID(sceGuClearDepth,(int a))
IMPL_VOID(sceGuClear,(int a))
IMPL_VOID(sceGuTerm,(void))
IMPL_VOID(sceGuSwapBuffers,(void))
IMPL_VOID(sceDisplayWaitVblankStart,(void))
IMPL_VOID(sceGumMatrixMode,(int a))
IMPL_VOID(sceGumLoadIdentity,(void))
IMPL_VOID(sceGumPerspective,(float a,float b,float c,float d))
void sceGumLookAt(const ScePspFVector3 *a,const ScePspFVector3 *b,const ScePspFVector3 *c){
 (void)c;eye=*a;forward=(ScePspFVector3){b->x-a->x,b->y-a->y,b->z-a->z};
 float len=sqrtf(forward.x*forward.x+forward.y*forward.y+forward.z*forward.z);
 forward.x/=len;forward.y/=len;forward.z/=len;
 mock_camera_eye=eye;mock_camera_forward=forward;
}
IMPL_VOID(sceKernelDcacheWritebackRange,(const void *p,unsigned int n))

IMPL_VOID(sceGuTexMode,(int a,int b,int c,int d))
IMPL_VOID(sceGuTexImage,(int a,int b,int c,int d,const void*p))
IMPL_VOID(sceGuTexFunc,(int a,int b))
IMPL_VOID(sceGuTexFilter,(int a,int b))
IMPL_VOID(sceGuTexWrap,(int a,int b))
IMPL_VOID(sceGuTexScale,(float a,float b))
IMPL_VOID(sceGuTexOffset,(float a,float b))

IMPL_VOID(sceGuBlendFunc,(int a,int b,int c,unsigned d,unsigned e))
