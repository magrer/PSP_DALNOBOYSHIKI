#ifndef ROADHAUL_SOUND_H
#define ROADHAUL_SOUND_H
void rh_sound_start(void);
void rh_sound_update(float rpm,float throttle,float speed,int stalled);
void rh_sound_stop(void);
#endif
