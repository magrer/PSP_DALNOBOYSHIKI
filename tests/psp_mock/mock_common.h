#ifndef PSP_MOCK_COMMON
#define PSP_MOCK_COMMON
#include <stddef.h>
#include <stdint.h>
typedef unsigned SceSize;
typedef struct {float x,y,z;} ScePspFVector3;
typedef struct {unsigned Buttons; unsigned char Lx,Ly;} SceCtrlData;
#define PSP_MODULE_INFO(a,b,c,d)
#define PSP_MODULE_USER 0
#define PSP_MAIN_THREAD_ATTR(a)
#define THREAD_ATTR_USER 0
#define THREAD_ATTR_VFPU 0
#define PSP_CTRL_MODE_ANALOG 0
#define PSP_CTRL_CROSS (1u<<0)
#define PSP_CTRL_SQUARE (1u<<1)
#define PSP_CTRL_TRIANGLE (1u<<2)
#define PSP_CTRL_CIRCLE (1u<<3)
#define PSP_CTRL_START (1u<<4)
#define PSP_CTRL_SELECT (1u<<5)
#define PSP_CTRL_UP (1u<<6)
#define PSP_CTRL_DOWN (1u<<7)
#define PSP_CTRL_LTRIGGER (1u<<8)
#define PSP_CTRL_RTRIGGER (1u<<9)
#define GU_DIRECT 1
#define GU_PSM_8888 2
#define GU_SCISSOR_TEST 3
#define GU_DEPTH_TEST 4
#define GU_GEQUAL 5
#define GU_CULL_FACE 6
#define GU_CLIP_PLANES 7
#define GU_SMOOTH 8
#define GU_TEXTURE_2D 9
#define GU_SYNC_FINISH 10
#define GU_SYNC_WHAT_DONE 11
#define GU_TRUE 12
#define GU_COLOR_BUFFER_BIT 13
#define GU_DEPTH_BUFFER_BIT 14
#define GU_PROJECTION 15
#define GU_VIEW 16
#define GU_MODEL 17
#define GU_TRIANGLES 18
#define GU_COLOR_8888 0x1c
#define GU_BLEND 31
#define GU_ADD 0
#define GU_SRC_ALPHA 2
#define GU_ONE_MINUS_SRC_ALPHA 3
#define GU_TEXTURE_32BITF 0x3
#define GU_TFX_MODULATE 0
#define GU_TCC_RGBA 1
#define GU_NEAREST 0
#define GU_REPEAT 0
#define GU_VERTEX_32BITF 0x180
#define GU_TRANSFORM_3D 0
#define GU_TRANSFORM_2D (1<<23)
#define GU_PI 3.14159265
unsigned long long sceKernelGetSystemTimeWide(void);
void sceGuInit(void);void sceGuStart(int,void*);void sceGuDrawBuffer(int,void*,int);
void sceGuDispBuffer(int,int,void*,int);void sceGuDepthBuffer(void*,int);
void sceGuOffset(int,int);void sceGuViewport(int,int,int,int);void sceGuDepthRange(int,int);
void sceGuScissor(int,int,int,int);void sceGuEnable(int);void sceGuDisable(int);
void sceGuDepthFunc(int);void sceGuShadeModel(int);void sceGuFinish(void);
void sceGuSync(int,int);void sceGuDisplay(int);void sceGuClearColor(unsigned);
void sceGuClearDepth(int);void sceGuClear(int);void sceGuDrawArray(int,int,int,const void*,const void*);
void sceGuBlendFunc(int,int,int,unsigned,unsigned);
void sceGuTexMode(int,int,int,int);void sceGuTexImage(int,int,int,int,const void*);
void sceGuTexFunc(int,int);void sceGuTexFilter(int,int);void sceGuTexWrap(int,int);
void sceGuTexScale(float,float);void sceGuTexOffset(float,float);
void sceGuTerm(void);void sceGuSwapBuffers(void);void sceDisplayWaitVblankStart(void);
void sceGumMatrixMode(int);void sceGumLoadIdentity(void);void sceGumPerspective(float,float,float,float);
void sceGumLookAt(const ScePspFVector3*,const ScePspFVector3*,const ScePspFVector3*);
void sceGumDrawArray(int,int,int,const void*,const void*);
void sceKernelDcacheWritebackRange(const void*,unsigned int);
int sceKernelCreateCallback(const char*,int (*)(int,int,void*),void*);
int sceKernelCreateThread(const char*,int (*)(SceSize,void*),int,int,int,void*);
int sceKernelRegisterExitCallback(int);int sceKernelSleepThreadCB(void);
int sceKernelStartThread(int,SceSize,void*);void sceKernelExitGame(void);
int scePowerSetClockFrequency(int,int,int);int sceCtrlSetSamplingCycle(int);
int sceCtrlSetSamplingMode(int);int sceCtrlPeekBufferPositive(SceCtrlData*,int);
#endif
