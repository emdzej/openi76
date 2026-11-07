/* The game's clock (clock.h). The frame's tick (0x450fa0) is not ported yet. */
#include "clock.h"
#include "render/fpu.h"
#include <string.h>

float g_clock_time;
float g_clock_dt = 0.06f;     /* 0x3d75c28f, as 0x450e90 sets them */
float g_clock_step = 0.06f;

void Timer_Init(float t[3], float period)
{
    int32_t b;
    memcpy(&b, &period, 4);
    if (b > 0x38d1b717) t[0] = fpu_f32(fpu_div(1.0, period));   /* 0x4a606c */
    else b = 0x7149f2ca, memcpy(&t[0], &b, 4);                   /* 1e30 */
    t[1] = t[2] = 0;
}
