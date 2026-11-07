/* The game's clock (0x450e30-0x4512d0): the frame's time step and the mission's time. */
#pragma once

extern float g_clock_time;   /* 0x4db140: the mission's time in seconds (0x450f40) */
extern float g_clock_dt;     /* 0x4db12c: the frame's step (0x450e50) */
extern float g_clock_step;   /* 0x4db138: the step as clamped to [0.001, 0.2] (0x450f30); 0.06 at first */

/* Timer_Init 0x451210: t[0] the rate 1 / period (1e30 for a period not above 1e-4), t[1] = t[2] = 0 */
void Timer_Init(float t[3], float period);
