/* The light environment (light.h). */
#include "render/light.h"
#include "exe.h"
#include "math3d.h"
#include "res.h"
#include "render/fpu.h"
#include <math.h>
#include <string.h>

int g_light_env;
int g_light_count;
const uint8_t *g_sun_map;
float g_sun[3];
float g_light_k[2];
Light g_lights[0x200];
static const char *sun_name;

#define ENV(i) (0x4d6858u + 0x14u * (uint32_t)(i))   /* {is_sun, name, elevation, strength, strength2} */

void Light_SetHour(int hour)
{
    /* the original clears the whole light block (0x550f70, 0xd884 bytes): the dynamic lights */
    g_light_count = 0;
    memset(g_lights, 0, sizeof g_lights);
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

static uint32_t bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float from_bits(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

/* In the current rounding mode. The bounds compare the floats' bits as signed integers. The angle's
   cosine and logarithm are the x87's (fcos, fyl2x: extended precision), here the host's in double. */
void Light_Add(const void *node, uint32_t *node_flags, const float *frame, int type, float range,
               float angle, const float *dir, const float *pos)
{
    if (exe_u32(ENV(g_light_env)) == 1 || g_light_count >= 0x200) return;
    for (int i = 0; i < g_light_count; i++)
        if (g_lights[i].node == node) return;
    Light *l = &g_lights[g_light_count++];
    if ((int32_t)bits(range) < 0x41a00000) range = 20.0f;
    if ((int32_t)bits(angle) < 0x3e32b8c2) angle = from_bits(0x3e32b8c2);
    if ((int32_t)bits(angle) > 0x3fc90fda) angle = from_bits(0x3fc90fda);
    double x = fpu_mul(fpu_add(cos(angle), 1.0), 0.5);   /* 0x4a5e80, 0x4a5e88 */
    double e = floor(fpu_div(-0.69315, log(x)));          /* 0x4a5e90 */
    *node_flags |= 0x800;
    l->node = node;
    if (type == 0) l->flags = 0;
    else if (type == 1) l->flags = 1;
    l->flags |= 8;
    l->range = range;
    l->inv_r2 = fpu_f32(fpu_div(1.0, fpu_mul(range, range)));   /* 0x4a5e98 */
    l->exponent = (float)e;
    if (pos) memcpy(l->pos, pos, sizeof l->pos);
    else memset(l->pos, 0, sizeof l->pos);
    if (type == 1) {
        const float *d = dir ? dir : frame + 6;
        for (int k = 0; k < 3; k++) l->dir[k] = -d[k];
    } else {
        memset(l->dir, 0, sizeof l->dir);
    }
}
