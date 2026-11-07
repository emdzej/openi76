/* The light environment (light.h). */
#include "render/light.h"
#include "exe.h"
#include "math3d.h"
#include "res.h"
#include <string.h>

int g_light_env;
int g_light_count;
const uint8_t *g_sun_map;
float g_sun[3];
float g_light_k[2];
static const char *sun_name;

#define ENV(i) (0x4d6858u + 0x14u * (uint32_t)(i))   /* {is_sun, name, elevation, strength, strength2} */

static float exe_f32(uint32_t va)
{
    float f;
    memcpy(&f, exe_data(va, 4), 4);
    return f;
}

void Light_SetHour(int hour)
{
    /* the original clears the whole light block (0x550f70, 0xd884 bytes): the dynamic lights */
    g_light_count = 0;
    uint32_t k0 = 0x3f3fe4a6, k1 = 0x39dad16a;
    memcpy(&g_light_k[0], &k0, 4);
    memcpy(&g_light_k[1], &k1, 4);
    g_light_env = ((hour + 2) % 24) * 8 / 24;   /* C's %: a negative hour gives a negative environment */
    sun_name = exe_str(exe_u32(ENV(g_light_env) + 4));
    g_sun_map = Res_Get(sun_name, NULL);
    Frame f;
    Frame_AxisAngle(&f, exe_f32(ENV(g_light_env) + 8), exe_f32(0x4d6848), exe_f32(0x4d684c), exe_f32(0x4d6850));
    memcpy(g_sun, &f.m[3], sizeof g_sun);
}

void Light_Shutdown(void)
{
    /* the original hands Res_Release 0x492570 the map's bytes as its name: the width (a small number) is
       no name in the paks or the cache, so nothing is released and the map stays counted */
    g_sun_map = NULL;
    sun_name = NULL;
    g_light_env = g_light_count = 0;
}

int Light_IsSun(void) { return (int)exe_u32(ENV(g_light_env)); }
float Light_Elevation(void) { return exe_f32(ENV(g_light_env) + 8); }
float Light_Strength(void) { return exe_f32(ENV(g_light_env) + 0xc); }
float Light_Strength2(void) { return exe_f32(ENV(g_light_env) + 0x10); }
