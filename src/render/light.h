/* The light environment (0x43a090-0x43a1c0): the time of day picks one of eight environments of the table
   at 0x4d6858 (read from the exe), each {sun or moon, its sky map, its elevation, two strengths}; the sun's
   direction is the elevation turned about z. The dynamic lights (0x43a1f0 on) are not ported yet. */
#pragma once
#include <stdint.h>

extern int g_light_env;            /* 0x550f70: 0-7 */
extern int g_light_count;          /* 0x550f74: dynamic lights */
extern const uint8_t *g_sun_map;   /* 0x5e0450: the sun's or moon's .map (Res_Get) */
extern float g_sun[3];             /* 0x5e0460: the sun's direction */
extern float g_light_k[2];         /* 0x5e046c, 0x5e0470 */

/* Light_SetHour 0x43a0d0: environment ((hour + 2) % 24) * 8 / 24 */
void Light_SetHour(int hour);
/* Light_Shutdown 0x43a1c0 */
void Light_Shutdown(void);
/* the environment's fields: Light_IsSun 0x43a090 (+0), Light_Elevation 0x43a0a0 (+8), Light_Strength
   0x43a0c0 (+0xc), Light_Strength2 0x43a0b0 (+0x10) */
int Light_IsSun(void);
float Light_Elevation(void);
float Light_Strength(void);
float Light_Strength2(void);
