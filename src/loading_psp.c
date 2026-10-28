#include <pspgu.h>
#include <pspkernel.h>
#include <pspdisplay.h>
#include "loading.h"
#include "loading_psp.h"
static unsigned int __attribute__((aligned(16))) loading_commands[1024];
typedef struct { unsigned short u,v; short x,y,z; } SplashVertex;
static const SplashVertex __attribute__((aligned(16))) vertices[2]={{0,0,0,0,0},{480,272,480,272,0}};
void rh_loading_screen(void){
 sceKernelDcacheWritebackRange(rh_loading_pixels,sizeof rh_loading_pixels);
 sceKernelDcacheWritebackRange(vertices,sizeof vertices);
 sceGuStart(GU_DIRECT,loading_commands);
 sceGuDisable(GU_DEPTH_TEST);sceGuDisable(GU_CLIP_PLANES);sceGuEnable(GU_TEXTURE_2D);
 sceGuTexMode(GU_PSM_5650,0,0,0);sceGuTexImage(0,512,512,512,rh_loading_pixels);
 sceGuTexFunc(GU_TFX_REPLACE,GU_TCC_RGB);sceGuTexFilter(GU_LINEAR,GU_LINEAR);
 sceGuTexScale(1.0f,1.0f);sceGuTexOffset(0,0);
 sceGuDrawArray(GU_SPRITES,GU_TEXTURE_16BIT|GU_VERTEX_16BIT|GU_TRANSFORM_2D,2,0,vertices);
 sceGuDisable(GU_TEXTURE_2D);sceGuEnable(GU_DEPTH_TEST);sceGuEnable(GU_CLIP_PLANES);
 sceGuFinish();sceGuSync(GU_SYNC_FINISH,GU_SYNC_WHAT_DONE);
 sceDisplayWaitVblankStart();sceGuSwapBuffers();
 sceKernelDelayThread(1500000);
}
