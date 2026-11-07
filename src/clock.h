/* The game's clock (0x450e30-0x4512d0): the frame's time step and the mission's time. */
#pragma once

extern float g_clock_time;   /* 0x4db140: the mission's time in seconds (0x450f40) */
extern float g_clock_dt;     /* 0x4db12c: the frame's step (0x450e50) */
extern float g_clock_step;   /* 0x4db138: the step as clamped to [0.001, 0.2] (0x450f30); 0.06 at first */

/* Timer_Init 0x451210: t[0] the rate 1 / period (1e30 for a period not above 1e-4), t[1] = t[2] = 0 */
void Timer_Init(float t[3], float period);
/* Clock_Init 0x450e90 (each mission): the steps at 0.06, the time at 0, the sub-steps' fractions 1 / n */
void Clock_Init(void);
/* Timer_Count 0x451250 (dt the frame's step, 0x4db12c) and 0x451290: t[2] = ftol(t[0] dt) + 1 sub-steps (at
   most 20), each t[1] = dt / t[2] */
void Timer_Count(float t[3], float dt);
/* Timer_Next 0x4512d0: the next sub-step's length, 0 when there are none left */
float Timer_Next(float t[3]);
