/* Simple procedural diesel engine sound. User-mode audio channel only.
   No external media, filesystem, flash, kernel patches or plugins. */
#include <pspkernel.h>
#include <pspaudio.h>
#include <stdint.h>
#include "sound_psp.h"
static volatile int playing=0;
static volatile float motor_rpm=650,motor_load=0,motor_speed=0;
static volatile int motor_stalled=0;
static int audio_tid=-1;
static int audio_thread(SceSize args,void *argp){
 (void)args;(void)argp;
 const int frames=1024;
 int ch=sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL,frames,PSP_AUDIO_FORMAT_STEREO);
 if(ch<0)return 0;
 static int16_t soundbuf[2048] __attribute__((aligned(64)));
 float p=0,p2=0,filtered=0;
 uint32_t noise=0x342a6b2fu;
 while(playing){
  float rpm=motor_rpm,load=motor_load,speed=motor_speed;
  float freq=(rpm/60.0f)*2.0f;
  if(freq<15)freq=15;if(freq>100)freq=100;
  float dp=freq/44100.0f;
  float volume=motor_stalled?.06f:(.26f+load*.37f+speed*.004f);
  if(volume>.73f)volume=.73f;
  for(int i=0;i<frames;i++){
   p+=dp;p2+=dp*.5f;
   if(p>=1.0f)p-=1.0f;
   if(p2>=1.0f)p2-=1.0f;
   float tri=p<.5f?(p*4.0f-1.0f):(3.0f-p*4.0f);
   float bass=p2<.5f?(p2*4.0f-1.0f):(3.0f-p2*4.0f);
   noise^=noise<<13;noise^=noise>>17;noise^=noise<<5;
   float hiss=((float)(noise&255)/128.0f-1.0f)*(.04f+load*.045f);
   float mix=(tri*.64f+bass*.25f+hiss)*volume;
   filtered=filtered*.56f+mix*.44f;
   int16_t sample=(int16_t)(filtered*10500.0f);
   soundbuf[i*2]=soundbuf[i*2+1]=sample;
  }
  sceAudioOutputBlocking(ch,0x7100,soundbuf);
 }
 sceAudioChRelease(ch);
 return 0;
}
void rh_sound_start(void){
 if(playing)return;
 playing=1;
 audio_tid=sceKernelCreateThread("RoadHaul Sound",audio_thread,0x18,0x2000,0,0);
 if(audio_tid<0){playing=0;return;}
 if(sceKernelStartThread(audio_tid,0,0)<0){playing=0;sceKernelDeleteThread(audio_tid);audio_tid=-1;}
}
void rh_sound_update(float rpm,float throttle,float speed,int stalled){
 motor_rpm=rpm;motor_load=throttle;motor_speed=speed;motor_stalled=stalled;
}
void rh_sound_stop(void){
 if(!playing)return;
 playing=0;
 if(audio_tid>=0){sceKernelWaitThreadEnd(audio_tid,0);sceKernelDeleteThread(audio_tid);audio_tid=-1;}
}
