/* The game's clock (clock.h). The frame's tick (0x450fa0) is not ported yet. */
#include "clock.h"
#include "render/fpu.h"
#include <string.h>

float g_clock_time;
float g_clock_dt = 0.06f;     /* 0x3d75c28f, as 0x450e90 sets them */
float g_clock_step = 0.06f;

/* 0x55f748: 1 / n for the sub-steps 1-19 (0x450e90, in the current rounding mode); entry 0 is never set
   and entry 20 is the next word, 0x55f798 (a shuffled list's count, 0 so far): a timer that needs 20
   steps gets steps of (nearly) nothing */
static float step_frac[21];

void Clock_Init(void)
{
    uint32_t b = 0x3d75c28f;
    memcpy(&g_clock_dt, &b, 4);
    memcpy(&g_clock_step, &b, 4);
    g_clock_time = 0;
    for (int i = 1; i < 20; i++) step_frac[i] = fpu_f32(fpu_div(1.0f, (double)i));   /* 0x4a606c */
}

void Timer_Count(float t[3], float dt)
{
    int32_t n = (int32_t)(int64_t)fpu_mul(t[0], dt) + 1;   /* _ftol */
    if (n > 20) n = 20;
    memcpy(&t[2], &n, 4);
    t[1] = fpu_f32(fpu_mul(step_frac[n < 0 ? 0 : n], dt));
}

float Timer_Next(float t[3])
{
    int32_t n;
    memcpy(&n, &t[2], 4);
    if (!n) return 0;   /* 0x4a6060 */
    n--;
    memcpy(&t[2], &n, 4);
    return t[1];
}

void Timer_Init(float t[3], float period)
{
    int32_t b;
    memcpy(&b, &period, 4);
    if (b > 0x38d1b717) t[0] = fpu_f32(fpu_div(1.0, period));   /* 0x4a606c */
    else b = 0x7149f2ca, memcpy(&t[0], &b, 4);                   /* 1e30 */
    t[1] = t[2] = 0;
}
