#ifndef RH_RENDER_PSP_H
#define RH_RENDER_PSP_H
#include "sim.h"
void rh_render_init(void);
void rh_render_frame(const Game *g,int looking);
void rh_render_end(void);
#endif
