/* The game's clock (clock.h), with network play's branches of the tick (0x461900). */
#include "clock.h"
#include "render/fpu.h"
#include <string.h>

#include "platform.h"
#include "net/netgame.h"

float g_clock_time;
float g_clock_dt = 0.06f;     /* 0x3d75c28f, as 0x450e90 sets them */
float g_clock_step = 0.06f;
float g_clock_inv = 16.666668f, g_clock_game;
uint32_t g_play_state = 1;

static float inv_step = 16.666668f;   /* 0x4db13c: 1 / g_clock_step */
static float last;                    /* 0x55f738: the previous tick, in seconds */
float g_clock_total;                  /* 0x55f73c: the steps summed after the first three (0x450e80) */
static int32_t warm;                  /* 0x55f740 */
static int32_t frames;                /* 0x55f744 */
static float sync_at, sync_by;        /* 0x4db144, 0x4db148: the keeper's game time to take, and its jump */
static int sync_due;                  /* 0x4db14c */

void Clock_Sync(float t, int reset)
{
    float lat = NetPl_Clock(NetPl_Keeper());
    sync_at = fpu_f32((double)lat + t);
    sync_by = fpu_f32(fpu_sub(fpu_add(lat, t), g_clock_game));
    if (reset) sync_by = 0;
    sync_due = 1;
}

/* GetTickCount in seconds (fild of the unsigned count, times 0.001 at 0x4a605c) */
double g_clock_fake_ms = -1;
static float now_s(void)
{
    double ms = g_clock_fake_ms >= 0 ? g_clock_fake_ms : plat_ms();
    return fpu_f32(fpu_mul((double)(uint32_t)(uint64_t)ms, 0.001f));
}
static int32_t bits(float f) { int32_t b; memcpy(&b, &f, 4); return b; }
static float from(uint32_t b) { float f; memcpy(&f, &b, 4); return f; }

/* 0x55f748: 1 / n for the sub-steps 1-19 (0x450e90, in the current rounding mode); entry 0 is never set
   and entry 20 is the next word, 0x55f798 (a shuffled list's count, 0 so far): a timer that needs 20
   steps gets steps of (nearly) nothing */
static float step_frac[21];

void Clock_Init(void)
{
    uint32_t b = 0x3d75c28f;
    memcpy(&g_clock_dt, &b, 4);
    memcpy(&g_clock_step, &b, 4);
    g_clock_inv = inv_step = from(0x41855556);   /* 16.67 */
    g_clock_total = 0;
    warm = frames = 0;
    g_clock_game = 0;
    g_clock_time = 0;
    last = now_s();
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

void Clock_Tick(void)
{
    frames++;
    float now = now_s();
    int net = Net_On();
    if ((g_play_state & 0x3e) && !net) {   /* paused (and not in a network game): no time passes */
        g_clock_dt = g_clock_step = 0;
        last = now;
        inv_step = g_clock_inv = from(0x7149f2ca);
        return;
    }
    g_clock_step = fpu_f32(fpu_sub(now, last));
    if (warm < 3) warm++;   /* 0x4a6058 */
    else g_clock_total = fpu_f32(fpu_add(g_clock_step, g_clock_total));
    if (!net) {   /* a network game's step is not bounded */
        if (bits(g_clock_step) < 0x3a83126f) g_clock_step = from(0x3a83126f);        /* 0.001 */
        else if (bits(g_clock_step) > 0x3e4ccccd) g_clock_step = from(0x3e4ccccd);   /* 0.2 */
    }
    g_clock_time = fpu_f32(fpu_add(g_clock_step, g_clock_time));
    uint32_t u = (uint32_t)bits(g_clock_step);
    if (u < 0xb8d1b717u && (int32_t)u < 0x38d1b717) inv_step = from(0x7149f2ca);   /* within 1e-4 of 0 */
    else inv_step = fpu_f32(fpu_div(1.0f, g_clock_step));
    last = now;
    if (!net || NetPl_AmKeeper()) {
        g_clock_game = fpu_f32(fpu_add(g_clock_game, g_clock_step));
        g_clock_dt = g_clock_step;
        g_clock_inv = inv_step;
    } else {   /* the others keep the keeper's game time (XX, Clock_Sync): a jump back is held at 0 */
        float game;
        if (!sync_due) {
            g_clock_dt = g_clock_step;
            game = fpu_f32(fpu_add(g_clock_game, g_clock_step));
        } else {
            g_clock_dt = fpu_f32(fpu_add(g_clock_step, sync_by));
            sync_due = 0;
            game = fpu_f32(fpu_add(g_clock_step, sync_at));
            if (g_clock_dt < 0.0f) {   /* 0x4a6060 */
                game = fpu_f32(fpu_add(fpu_add(g_clock_step, sync_at), g_clock_step));
                g_clock_dt = g_clock_game <= game ? fpu_f32(fpu_sub(game, g_clock_game)) : 0.0f;
            }
        }
        g_clock_game = game;
        uint32_t v = (uint32_t)bits(g_clock_dt);
        if (v < 0xb8d1b717u && (int32_t)v < 0x38d1b717) g_clock_inv = from(0x7149f2ca);
        else g_clock_inv = fpu_f32(fpu_div(1.0f, g_clock_dt));
    }
    if (bits(g_clock_dt) > 0x3f800000) g_clock_dt = g_clock_inv = 1.0f;
}
