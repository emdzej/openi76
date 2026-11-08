/* The AI's orders and tactics (orders.h). In the current rounding mode (render/fpu.h). */
#include "orders.h"
#include "sound.h"
#include "ai.h"
#include "avoid.h"
#include "clock.h"
#include "combat.h"
#include "weapons.h"
#include "crt.h"
#include "damage.h"
#include "drive.h"
#include "entity.h"
#include "exe.h"
#include "fsm.h"
#include "math3d.h"
#include "nav.h"
#include "physics.h"
#include "roads.h"
#include "roadwar.h"
#include "platform.h"
#include "terrain.h"
#include "render/fpu.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

Node *g_ai_user;

static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }
static void put32(uint8_t *p, int32_t v) { memcpy(p, &v, 4); }
static void putf(uint8_t *p, float v) { memcpy(p, &v, 4); }
static float rdf(const uint8_t *p) { float v; memcpy(&v, p, 4); return v; }
#define F32 fpu_f32
#define MUL fpu_mul
#define ADD fpu_add
#define SUB fpu_sub
#define DIV fpu_div
static bool below(double a, double b) { return !(a >= b); }   /* x87: unordered counts */
static bool below_eq_d(double a, double b) { return !(a > b); }
static uint8_t *ai_of(Node *n) { return *Data_Ptr(n->data, 0x108); }   /* 0x42d380 */

/* ---- small vehicle steps ---- */

void Vehicle_SetMode(Node *n, int32_t mode)
{
    if (n->cls == 1) put32(n->data + 0x104, mode);
}

/* 0x4683c0: a sound at the node */
static void sound(int id, Node *n) { Sound_Event(id, n, NULL); }

void Engine_Stop(Node *n)
{
    uint8_t *d = n->data;
    if (!(rd32(d + 0x454) & 1)) return;
    sound(2, n);
    put32(d + 0x454, rd32(d + 0x454) & ~1);
}

void Engine_Start(Node *n)
{
    uint8_t *d = n->data;
    if (rd32(d + 0x454) & 9) return;
    putf(d + 0x450, Engine_StartTime(n));
    put32(d + 0x454, (int32_t)((uint32_t)rd32(d + 0x454) | 0x80000008u));
}

void AI_TouchUser(Node *n)
{
    if (n == g_ai_user) g_user_pressed = 1;   /* 0x505494 */
}

float Vehicle_Throttle(Node *n, float want)
{
    if (View_Distance(n, 25.0f) == 2 || n->cls == 9) return want;   /* 0x42b810 */
    float f[3] = {n->frame.m[6], n->frame.m[7], n->frame.m[8]};
    uint32_t flags = (uint32_t)rd32(n->data + 0x454);
    float half = exe_f32(0x4a6758);
    float x = fpu_f32(fpu_mul(want, half)), step = x, best = 0;
    for (int i = 0; i < 6; i++) {
        step = fpu_f32(fpu_mul(step, half));
        float acc;
        int32_t slide = 0;
        Vehicle_Force(n, x, 0, f, flags, &acc, &slide);
        if (slide) x = fpu_f32(fpu_sub(x, step));
        else {
            best = x;
            x = fpu_f32(fpu_add(x, step));
        }
    }
    return best;
}

void AI_Stop(Node *n)
{
    AI_TouchUser(n);
    putf(n->data + 0xe4, fpu_f32(fpu_mul(Vehicle_Throttle(n, -1.0f), exe_f32(0x4a51c8))));
}

/* ---- the tactics' functions ---- */

/* sit's frame 0x40a580 (also guard's and hide's): standing still, braking. A vehicle only; a dead one is
   noted once (+4; the reporter is a no-op) */
static int sit_frame(Node *n, Node *t)
{
    (void)t;
    if (!(n->cls == 1 || n->cls == 8 || n->cls == 9)) {   /* 0x42e570 */
        plat_log(exe_str(0x4a89fc));   /* the original's fatal */
        return 0;
    }
    uint8_t *d = n->data, *ai = ai_of(n);
    if (!rd32(ai + 4) && (rd32(d + 0x454) & 0x20)) put32(ai + 4, 1);
    put32(d + 0xe0, 0);
    put32(d + 0xe4, (int32_t)0xbf800000);
    AI_TouchUser(n);
    return 0;
}

/* hide's enter 0x40a600 and leave 0x40a620: the engine off and the mode 0, then on and the mode 3 */
static int hide_enter(Node *n, Node *t) { (void)t; Engine_Stop(n); Vehicle_SetMode(n, 0); return 0; }
static int hide_leave(Node *n, Node *t) { (void)t; Engine_Start(n); Vehicle_SetMode(n, 3); return 0; }

/* ---- goto, evade and race (0x16, 0x18, 0x1a), and following the path (0x17) ---- */

Node *g_nav_owner;   /* 0x505440: the vehicle whose search goes on */
int g_nav_asked;     /* 0x4a88c8: a vehicle waited for its turn this frame */
int g_nav_busy;      /* 0x4eec30: a search ran this frame */

/* 0x40f6a0: the script path at ai+0x8c becomes the one followed, from its first point */
static void path_set(uint8_t *ai)
{
    int32_t path = rd32(ai + 0x8c);
    put32(ai + 0xa0, 0);
    put32(ai + 0x88, path);
    put32(ai + 0xa4, Fsm_Word((uint32_t)path + 0x50));
    put32(ai + 0xa8, 0);
}

/* goto's and evade's enter 0x41d1c0: the path, a search to start (+0x84 2), forward, mode 3 */
static int goto_enter(Node *n, Node *t)
{
    (void)t;
    uint8_t *ai = ai_of(n), *d = n->data;
    path_set(ai);
    put32(ai + 0x84, 2);
    put32(ai + 0x9c, 0);
    putf(d + 0xe8, 1.0f);
    Vehicle_SetMode(n, 3);
    put32(d + 0xf0, 0);
    putf(d + 0xe8, 1.0f);
    Vehicle_SetMode(n, 3);
    return 0;
}

/* the frame of goto, evade and race 0x41d4a0: the search, one vehicle at a time (0x409a30: the one whose
   search goes on, else the first to ask this frame); meanwhile a quarter of the cruising speed, the
   wheels straight */
static int goto_frame(Node *n, Node *t)
{
    (void)t;
    uint8_t *d = n->data, *ai = ai_of(n);
    put32(ai + 0x9d00, (int32_t)0xc1200000);   /* -10 */
    bool turn = g_nav_owner ? n == g_nav_owner : !g_nav_asked;
    if (!turn)
        g_nav_asked = 1;   /* 0x409a60 */
    else {
        put32(ai + 0x94, 0);
        if (n->cls == 9) {   /* straight at the script's point */
            int32_t at = rd32(ai + 0xa0);
            uint32_t pts = (uint32_t)Fsm_Word((uint32_t)rd32(ai + 0x88) + 0x54);
            put32(ai + 0xa8, 0);
            put32(ai + 0xac, 1);
            put32(ai + 0xb0, Fsm_Word(pts + (uint32_t)at * 12));
            put32(ai + 0xb4, Fsm_Word(pts + (uint32_t)at * 12 + 8));
            put32(ai + 0xb8, 0);
            put32(ai + 0x84, 0);
        } else {
            int32_t count = 0;
            put32(ai + 0x84, Nav_Search(ai, &count));
        }
        g_nav_busy = 1;   /* 0x409a70 */
        g_nav_owner = rd32(ai + 0x84) == 1 ? n : NULL;
    }
    if (!rd32(ai + 0x84)) {
        put32(ai + 0x98, 0);
        return 0;
    }
    AI_Speed(n, F32(MUL(AI_Cruise(n), 0.25f)));   /* 0x4a575c */
    put32(d + 0xe0, 0);   /* 0x40a3f0 */
    return 0;
}

/* 0x41d5d0: the search is done */
static int searched(Node *n, Node *t) { (void)t; return !rd32(ai_of(n) + 0x84); }
/* 0x41d5f0: the script path ran out (+0x9c) or another was given (+0x8c) */
static int path_changed(Node *n, Node *t)
{
    (void)t;
    uint8_t *ai = ai_of(n);
    return rd32(ai + 0x9c) || rd32(ai + 0x8c) != rd32(ai + 0x88);
}

/* following the path's enter 0x41d620: no hand brake, forward, mode 3 */
static int follow_enter(Node *n, Node *t)
{
    (void)t;
    put32(n->data + 0xf0, 0);
    putf(n->data + 0xe8, 1.0f);
    Vehicle_SetMode(n, 3);
    return 0;
}

/* following the path's frame 0x41d650: the point driven to, the heading 11 m along the path, the order's
   speed (+0x80), the cruising one when steering hard (over 0.45) */
static int follow_frame(Node *n, Node *t)
{
    (void)t;
    uint8_t *ai = ai_of(n), *d = n->data;
    Path_Advance(ai);
    AI_Steer(n, Path_Aim(n, ai, 0, 11.0f));
    putf(d + 0xe8, 1.0f);
    Vehicle_SetMode(n, 3);
    if (n->cls != 9 && fabs(rdf(d + 0xe0)) > 0.44999998807907104) AI_Speed(n, AI_Cruise(n));   /* 0x4a5760 */
    else AI_Speed(n, rdf(ai + 0x80));
    return 0;
}

/* 0x41d6f0: the path's end reached, the script's point passed, or the path ran out */
static int follow_done(Node *n, Node *t)
{
    (void)t;
    uint8_t *ai = ai_of(n);
    return !(!rd32(ai + 0x98) && !rd32(ai + 0x94) && rd32(ai + 0xac) > rd32(ai + 0xa8));
}

/* ---- rolling back (0x10) and stuck (0x4189c0) ---- */

/* the velocity along the vehicle's z axis, summed in the order each test has */
static double back_speed(Node *n, int pop)
{
    const uint8_t *d = n->data;
    const float *z = &n->frame.m[6];
    double x = MUL(rdf(d + 0xbc), z[0]), y = MUL(rdf(d + 0xc0), z[1]), w = MUL(rdf(d + 0xc4), z[2]);
    return pop ? ADD(ADD(y, w), x) : ADD(ADD(w, x), y);
}

/* 0x4183a0: going forward (+0xe8 over 0) yet rolling back faster than 0.05 */
static int rolling_back(Node *n, Node *t)
{
    (void)t;
    if (rd32(n->data + 0xe8) <= 0) return 0;
    return ADD(back_speed(n, 0), 0.05) < 0.0;   /* 0x4a55a0, 0x4a55a8 */
}
/* 0x418400: no longer */
static int not_rolling_back(Node *n, Node *t)
{
    (void)t;
    if (rd32(n->data + 0xe8) <= 0) return 1;
    return !(ADD(back_speed(n, 1), 0.05) < 0.0);
}
/* rolling back's enter 0x418460: no hand brake, the wheels to full lock the other way */
static int back_enter(Node *n, Node *t)
{
    (void)t;
    uint8_t *d = n->data;
    put32(d + 0xf0, 0);
    if ((uint32_t)rd32(d + 0xe0) > 0x80000000u) putf(d + 0xe0, AI_SteerLimit(n));
    else putf(d + 0xe0, F32(-AI_SteerLimit(n)));
    return 0;
}
/* its frame 0x4184b0: braking, the wheels at full lock as they turn */
static int back_frame(Node *n, Node *t)
{
    (void)t;
    uint8_t *d = n->data;
    AI_Stop(n);
    if (rd32(d + 0xe0) > 0) putf(d + 0xe0, AI_SteerLimit(n));
    else putf(d + 0xe0, F32(-AI_SteerLimit(n)));
    return 0;
}

int32_t g_ai_frames;   /* 0x505518 (0x40fad0, 0x40fae0) */

/* 0x4189c0: stuck, the throttle that holds (0x474eb0 at full) at most 0.1 over the last four frames (a
   ring at +0x9d58 by the frame's count) */
static int stuck(Node *n, Node *t)
{
    (void)t;
    uint8_t *ai = ai_of(n);
    int32_t k = g_ai_frames % 4;
    putf(ai + 0x9d58 + k * 4, Vehicle_Throttle(n, 1.0f));
    for (int i = 0; i < 4; i++)
        if (rd32(ai + 0x9d58 + i * 4) >= 0x3dcccccd) return 0;
    return 2;
}

/* a tactic's function by its address */
/* ---- avoiding (0x11) ---- */

/* 0x40a3c0 / 0x40a390: full lock (the tyres' limit x 0.9999, 0x4a51c8) left / right; 0x40a3f0: straight */
static void lock(Node *n, int sign)
{
    double v = MUL(AI_SteerLimit(n), 0.9998999834060669f);
    putf(n->data + 0xe0, F32(sign < 0 ? -v : v));   /* fchs before the store: rounded as negative */
}

/* 0x40a410: the throttle a gain's way (+0xa81c) toward the most that holds, at most 0.75 + gain / 4 */
static void open_up(Node *n)
{
    AI_TouchUser(n);
    uint8_t *ai = ai_of(n), *d = n->data;
    float g = rdf(ai + 0xa81c), e4 = rdf(d + 0xe4);
    double full = Vehicle_Throttle(n, 1.0f);
    double cap = ADD(MUL(g, 0.25f), 0.75f);   /* 0x4a5190, 0x4a51cc */
    float v = F32(ADD(MUL(SUB(full, e4), g), e4));
    if (cap < v) v = F32(cap);
    putf(d + 0xe4, v);
}

/* the time to look again: the game's time + (1 - steering gain) x 2 + extra (0x4a5564, 0x4a55e4) */
static float again(uint8_t *ai, float extra)
{
    return F32(ADD(ADD(g_clock_game, MUL(SUB(1.0f, rdf(ai + 0xa820)), 2.0f)), extra));
}

/* past something not yet near (t under 1 s) or a car: lock away from the steering, and for a car going 10-35
   m/s the cruising speed when it lies ahead (sector 7, 0 or 1), the throttle opened; over 35 the cruising
   speed */
static void swerve(Node *n, uint8_t *ai)
{
    uint8_t *d = n->data;
    if (rd32(d + 0xe0) >= 0 && (uint32_t)rd32(d + 0xe0) <= 0x80000000u) lock(n, -1);
    else lock(n, 1);
    Node *who = *(Node **)Data_Ptr(ai, 0x9d2c);
    if (who && who->cls == 1) {
        if (rd32(d + 0xac) > 0x411fffff) {
            if (rd32(d + 0xac) > 0x420c0000) {
                AI_Speed(n, AI_Cruise(n));
                putf(ai + 0x9d34, again(ai, 0.5f));
                return;
            }
            float h = AI_Heading(n);
            int sec = Angle_Sector(h, Pair_Angle(n, who));
            if (sec >= 0 && (sec < 2 || sec == 7)) AI_Speed(n, AI_Cruise(n));
        }
        open_up(n);
    }
    putf(ai + 0x9d34, again(ai, 0.5f));   /* 0x4a558c */
}

/* the words of the last look ahead's step frame at the way finder's two unset locals: by the depth of the
   push test that called it (0x418df0 m[2], m[5]; 0x418e70 m[3], m[6]; 0x418ee0 m[5], m[8]) */
static int g_way_words[2] = {5, 8};

/* avoid's enter 0x419070: forward, mode 3, no target; far from it (t of 1 s or more) a way round (0x416ef0)
   and the time to look again plus its factor; else swerve. +0x9d18 set (the next frame skipped), +0x9d1c
   the speed, +0x9d24 the time */
static int avoid_enter(Node *n, Node *t)
{
    (void)t;
    uint8_t *d = n->data, *ai = ai_of(n);
    put32(d + 0xf0, 0);
    Vehicle_SetMode(n, 3);
    put32(d + 0xe8, 0x3f800000);
    *Data_Ptr(ai, 0xa998) = NULL;
    if (rd32(ai + 0x9d28) > 0x3f7fffff) {
        AI_FindWay(n, ai, *(Node **)Data_Ptr(ai, 0x9d2c), (float *)(ai + 0x9d34), 0, 1, AI_ProbeWord(g_way_words[0]),
                   AI_ProbeWord(g_way_words[1]));
        putf(ai + 0x9d34, again(ai, rdf(ai + 0x9d34)));
    } else
        swerve(n, ai);
    put32(ai + 0x9d18, 1);
    put32(ai + 0x9d1c, rd32(d + 0xac));
    putf(ai + 0x9d24, g_clock_game);
    return 0;
}

/* avoid's frame 0x419210: every other frame, the look ahead with the controls: clear, straight on at the
   order's speed; else as at the start */
static int avoid_frame(Node *n, Node *t)
{
    (void)t;
    uint8_t *d = n->data, *ai = ai_of(n);
    if (rd32(ai + 0x9d18)) {
        put32(ai + 0x9d18, 0);
        return 0;
    }
    float slow, f;
    if (!AI_Avoid(n, rdf(d + 0xe0), rdf(d + 0xe4), (Node **)Data_Ptr(ai, 0x9d2c), &slow, &f, 0, 1,
                  (float *)(ai + 0x9d28))) {
        put32(d + 0xe0, 0);   /* 0x40a3f0 */
        AI_Speed(n, rdf(ai + 0x80));
        return 0;
    }
    if (rd32(ai + 0x9d28) > 0x3f7fffff) {
        AI_FindWay(n, ai, *(Node **)Data_Ptr(ai, 0x9d2c), &f, 0, 1, AI_ProbeWord(4), AI_ProbeWord(7));   /* its own look ahead's */
        putf(ai + 0x9d34, again(ai, f));
        put32(ai + 0x9d18, 0);
        AI_Speed(n, AI_Cruise(n));
        return 0;
    }
    swerve(n, ai);
    put32(ai + 0x9d18, 1);
    return 0;
}

/* avoid's leave 0x419410: avoiding for more than 3 s (0x4a55e8) marks the path's last point reached */
static int avoid_leave(Node *n, Node *t)
{
    (void)t;
    uint8_t *ai = ai_of(n);
    if (ADD(rdf(ai + 0x9d24), 3.0f) < g_clock_game) put32(ai + 0x98, 1);
    return 0;
}

/* 0x418290: the time to look again has passed */
static int looked(Node *n, Node *t) { (void)t; return g_clock_game > rdf(ai_of(n) + 0x9d34); }

/* the push tests to avoiding, while +0x9d14: 0x418df0 something ahead but the target (all round),
   0x418e70 something ahead (all round), 0x418ee0 something ahead (only cars while it turned less than a
   second ago, on a bridge or on rough ground (+0x45c 2, 3, 4, 7), or on 6 heading along the path within 1;
   a car ahead sounds the horn one time in four, Horn 0x476440) */
static int ahead_not_target(Node *n, Node *t)
{
    uint8_t *ai = ai_of(n), *d = n->data;
    if (!rd32(ai + 0x9d14)) return 0;
    Node **who = (Node **)Data_Ptr(ai, 0x9d2c);
    int r = AI_Avoid(n, rdf(d + 0xe0), rdf(d + 0xe4), who, (float *)(ai + 0x9d30), (float *)(ai + 0x9d34), 0, 1,
                     (float *)(ai + 0x9d28));
    g_way_words[0] = 2, g_way_words[1] = 5;
    return *who != t ? r : 0;
}

static int ahead(Node *n, Node *t)
{
    (void)t;
    uint8_t *ai = ai_of(n), *d = n->data;
    if (!rd32(ai + 0x9d14)) return 0;
    float slow;
    g_way_words[0] = 3, g_way_words[1] = 6;
    return AI_Avoid(n, rdf(d + 0xe0), rdf(d + 0xe4), (Node **)Data_Ptr(ai, 0x9d2c), &slow, (float *)(ai + 0x9d34), 0,
                    1, (float *)(ai + 0x9d28));
}

static int ahead_driving(Node *n, Node *t)
{
    (void)t;
    uint8_t *ai = ai_of(n), *d = n->data;
    if (!rd32(ai + 0x9d14)) return 0;
    int cars;
    int32_t at = rd32(ai + 0xa8);
    if (below(g_clock_game, ADD(rdf(ai + 0x9d00), 1.0f))) cars = 0;   /* 0x4a5564 */
    else if (rd32(ai + 0xbc + at * 0x14)) cars = 1;
    else
        switch (rd32(d + 0x45c)) {
        case 2: case 3: case 4: case 7:
            cars = 1;
            break;
        case 6: {
            cars = 0;
            float h = AI_Heading(n);
            if (at > 0) {
                const uint8_t *p = ai + 0xb0 + at * 0x14;
                float a = F32(atan2(SUB(rdf(p), rdf(p - 0x14)), SUB(rdf(p + 4), rdf(p - 0x10))));
                cars = below(fabs(Angle_Diff(h, a)), 1.0f);
            }
            break;
        }
        default:
            cars = 0;
        }
    float slow;
    Node **who = (Node **)Data_Ptr(ai, 0x9d2c);
    int r = AI_Avoid(n, rdf(d + 0xe0), rdf(d + 0xe4), who, &slow, (float *)(ai + 0x9d34), cars, 0,
                     (float *)(ai + 0x9d28));
    g_way_words[0] = 5, g_way_words[1] = 8;   /* what the horn's calls leave there is not modelled */
    if (r && *who && ((*who)->cls == 1 || (*who)->cls == 8 || (*who)->cls == 9)) {
        int k = crt_rand();
        if ((k & 3) == 0) Horn(n, 0);   /* 0x476440 */
    }
    return r;
}

/* 0x418a50, avoiding to stuck (0x12): not class 9; going forward yet backing at 0.05, or blocked (0x415690) */
static int blocked(Node *n, Node *t)
{
    (void)t;
    if (n->cls == 9) return 0;
    uint8_t *d = n->data;
    if (rd32(d + 0xe8) > 0) {
        const float *v = (const float *)(d + 0xbc), *z = &n->frame.m[6];
        float vv[3];
        memcpy(vv, v, 12);
        if (ADD(ADD(ADD(MUL(vv[1], z[1]), MUL(vv[2], z[2])), MUL(vv[0], z[0])), 0.05) < 0.0) return 1;   /* 0x4a55a0 */
    }
    return AI_Blocked(n) ? 1 : 0;
}

/* ---- stuck (0x12) and backing out (0x13) ---- */

static int32_t fbits(float f) { int32_t v; memcpy(&v, &f, 4); return v; }

/* the direction flipped (+0xe8 1 within 1e-4: back, mode 1; else forward, mode 3) */
static void flip(Node *n)
{
    uint8_t *d = n->data;
    float x = F32(SUB(rdf(d + 0xe8), 1.0f));
    if (!(x <= -9.999999747378752e-05f) && fbits(x) < 0x38d1b717) {
        put32(d + 0xe8, (int32_t)0xbf800000);
        Vehicle_SetMode(n, 1);
    } else {
        put32(d + 0xe8, 0x3f800000);
        Vehicle_SetMode(n, 3);
    }
}

/* 0x40a4b0: the throttle at the most that holds x 0.9999 */
static void full_throttle(Node *n)
{
    AI_TouchUser(n);
    putf(n->data + 0xe4, F32(MUL(Vehicle_Throttle(n, 1.0f), 0.9998999834060669f)));   /* 0x4a51c8 */
}

/* 0x417910: steer 0.95 of the tyres' limit away from the steering (as driven), the cruising speed up to 5
   m/s else the brakes, the direction flipped; look again in 2 s, the heading noted */
static void back_out(Node *n, uint8_t *d, uint8_t *ai)
{
    float lim = AI_SteerLimit(n);
    uint32_t dir = (uint32_t)rd32(d + 0xe8), e0 = (uint32_t)rd32(d + 0xe0);
    bool neg = dir <= 0x80000000u ? e0 > 0x80000000u : e0 <= 0x80000000u;
    putf(d + 0xe0, F32(MUL(lim, neg ? 0.949999988079071f : -0.949999988079071f)));   /* 0x4a5424, 0x4a5550 */
    if (rd32(d + 0xac) < 0x40a00001) AI_Speed(n, AI_Cruise(n));
    else AI_Stop(n);
    flip(n);
    putf(ai + 0x9d34, F32(ADD(g_clock_game, 2.0f)));   /* 0x4a54a4 */
    putf(ai + 0x9d38, AI_HeadingNow(n));   /* 0x4182c0: from the frame and the flipped direction, not the frame's cache */
}

/* stuck's enter 0x418500: no hand brake; the steering (+0x9d20) and the ground's height (+0x9d3c) noted;
   back out; look again in 4 s; the heading (+0x9d38) */
static int stuck_enter(Node *n, Node *t)
{
    (void)t;
    uint8_t *d = n->data, *ai = ai_of(n);
    put32(d + 0xf0, 0);
    put32(ai + 0x9d20, rd32(d + 0xe0));
    putf(ai + 0x9d3c, F32(Terrain_Height(n->frame.pos[0], n->frame.pos[2])));
    back_out(n, d, ai);
    putf(ai + 0x9d34, F32(ADD(g_clock_game, 4.0f)));   /* 0x4a55b0 */
    putf(ai + 0x9d38, AI_HeadingNow(n));   /* inline in the original, as below: the direction as flipped */
    return 0;
}

/* stuck's frame 0x418660: turned 1.5 from the noted heading: brake; going forward faster than 0.1: brake,
   straight; backing slower than 8 m/s: full throttle, straight; else steer 0.95 of the limit against the
   noted steering at the cruising speed */
static int stuck_frame(Node *n, Node *t)
{
    (void)t;
    uint8_t *d = n->data, *ai = ai_of(n);
    const float *v = (const float *)(d + 0xbc), *z = &n->frame.m[6];
    float vv[3];
    memcpy(vv, v, 12);
    float fwd = F32(ADD(ADD(MUL(vv[1], z[1]), MUL(vv[2], z[2])), MUL(vv[0], z[0])));
    float h = AI_HeadingNow(n);
    if (!below(fabs(Angle_Diff(h, rdf(ai + 0x9d38))), 1.5f)) {   /* 0x4a55b4 */
        AI_Stop(n);
        return 0;
    }
    if (fbits(fwd) > 0x3dcccccd) {
        AI_Stop(n);
        put32(d + 0xe0, 0);
        return 0;
    }
    if ((uint32_t)fbits(fwd) < 0xc1000000u) {
        full_throttle(n);
        put32(d + 0xe0, 0);
        return 0;
    }
    float l = F32(fabs(AI_SteerLimit(n)));
    putf(d + 0xe0, F32(MUL(l, rd32(ai + 0x9d20) > 0 ? -0.949999988079071f : 0.949999988079071f)));   /* 0x4a55bc, 0x4a55c0 */
    AI_Speed(n, AI_Cruise(n));
    return 0;
}

/* stuck's leave 0x418ad0: the stuck ring refilled (0x40faf0), brake, the direction flipped */
static int stuck_leave(Node *n, Node *t)
{
    (void)t;
    uint8_t *ai = ai_of(n);
    for (int k = 0; k < 4; k++) put32(ai + 0x9d58 + 4 * k, 0x3f800000);
    AI_Stop(n);
    flip(n);
    return 0;
}

/* stuck's pop 0x418850: the ground under it moved 3 m from the noted height, or turned 1.5 from the noted
   heading while under 1 m/s */
static int unstuck(Node *n, Node *t)
{
    (void)t;
    uint8_t *ai = ai_of(n), *d = n->data;
    float h = AI_HeadingNow(n);
    double diff = Angle_Diff(h, rdf(ai + 0x9d38));
    double g = Terrain_Height(n->frame.pos[0], n->frame.pos[2]);
    if (3.0 < fabs(SUB(rdf(ai + 0x9d3c), g))) return 1;   /* 0x4a55c8 */
    return fbits(F32(fabs(diff))) > 0x3fbfffff && rd32(d + 0xac) < 0x3f800000;
}

/* 0x418b60, stuck to backing out (0x13): the terrain forward blocks (0x415080, forward) and the ground 5 m
   ahead is less than 0.1 above here */
static int wall_ahead(Node *n, Node *t)
{
    (void)t;
    uint8_t *d = n->data;
    float p[3] = {F32(n->frame.pos[0]), F32(n->frame.pos[1]), F32(n->frame.pos[2])}, slow, tt;
    if (!AI_TerrainAhead(n, p, &slow, &tt)) return 0;
    float h0 = F32(Terrain_Height(p[0], p[2]));
    float sz = F32(MUL(n->frame.m[8], 5.0f)), sx = F32(MUL(n->frame.m[6], 5.0f));   /* 0x4a55dc */
    float kx = sx, kz = sz, dd = 5.0f;
    (void)d;
    for (;;) {
        float x = F32(ADD(p[0], kx));
        double h = Terrain_Height(x, ADD(p[2], kz));   /* x as a float, z unrounded */
        float hf = F32(h);
        if (below(SUB(h, h0), 0.10000000149011612f)) return 1;   /* 0x4a5590 */
        kz = F32(ADD(kz, sz));
        kx = F32(ADD(kx, sx));
        double nd = ADD(dd, 5.0f);
        h0 = hf;
        dd = F32(nd);
        if (!below_eq_d(nd, 5.099999904632568f)) return 0;   /* 0x4a55e0 */
    }
}

/* backing out's enter 0x418ce0: the direction flipped, straight, look again in 1.5 s; frame 0x418d70: full
   throttle; leave 0x418d80: the direction flipped */
static int backing_enter(Node *n, Node *t)
{
    (void)t;
    flip(n);
    put32(n->data + 0xe0, 0);
    putf(ai_of(n) + 0x9d34, F32(ADD(g_clock_game, 1.5f)));
    return 0;
}

static int backing_frame(Node *n, Node *t) { (void)t; full_throttle(n); return 0; }
static int backing_leave(Node *n, Node *t) { (void)t; flip(n); return 0; }

/* ---- attack (6) ---- */

/* attack's enter 0x417b00: a time 20-34 s on (+0xa82c), the target (+0xa998), forward, mode 3, the order's
   speed 35 */
static int attack_enter(Node *n, Node *t)
{
    uint8_t *d = n->data, *ai = ai_of(n);
    int r = crt_rand();
    putf(ai + 0xa82c, F32(ADD(ADD(g_clock_game, r % 15), 20.0f)));   /* 0x4a556c */
    *Data_Ptr(ai, 0xa998) = t;
    put32(d + 0xf0, 0);
    Vehicle_SetMode(n, 3);
    put32(d + 0xe8, 0x3f800000);
    put32(ai + 0x80, 0x420c0000);
    return 0;
}

/* attack's frame 0x417c00: Combat in mode 1 (its answer is whatever Combat left in eax; the caller drops it) */
static int attack_frame(Node *n, Node *t) { Combat(n, t, 1); return 0; }


/* ---- the road war (1-3), chasing down the road (0xa), hanging back (0x1c): roadwar.c ---- */

static bool vehicle(const Node *n) { return n && (n->cls == 1 || n->cls == 8 || n->cls == 9); }   /* 0x42e570 */
/* a time rand() % m + base seconds on (+0xa82c); the remainder stored as a float first */
static void after(uint8_t *ai, int m, float base)
{
    float r = (float)(crt_rand() % m);
    putf(ai + 0xa82c, F32(ADD(ADD(g_clock_game, r), base)));
}
/* the target (+0xa998) kept with chance +0xa818 in 4 (0x41c0c0, 0x41c410) */
static void maybe_target(uint8_t *ai, Node *t)
{
    *Data_Ptr(ai, 0xa998) = crt_rand() % 4 >= rd32(ai + 0xa818) ? NULL : t;
}

/* 0x4098f0: a has room to attack t: fewer than t's +0xa948 others (neither a nor t, not wrecked, the +0x454
   bit 5) have t as their target; always for t not a vehicle */
static int attack_room(Node *a, Node *t)
{
    if (!vehicle(t)) return 1;
    int32_t room = rd32(ai_of(t) + 0xa948), count = 0;
    for (int team = 0; team < AI_TEAMS; team++)
        for (int i = 0; i < g_team_count[team] && i < 0x40; i++) {
            Node *o = g_teams[team][i]->node;
            if (o == t || o == a || (rd32(o->data + 0x454) & 0x20)) continue;
            if (*Data_Ptr(ai_of(o), 0xa998) == t && ++count >= room) return 0;
        }
    return 1;
}
/* 0x4198c0: hang back: t is the user's vehicle and others fill its room */
static int crowded(Node *n, Node *t) { return g_user_entity && g_user_entity->node == t && !attack_room(n, t); }
/* 0x4198f0: room again */
static int room_again(Node *n, Node *t) { return attack_room(n, t); }

/* road war's enter 0x41c060 (tactic 1): t the target, 5 s, the order's speed 35 */
static int war_enter(Node *n, Node *t)
{
    uint8_t *ai = ai_of(n);
    *Data_Ptr(ai, 0xa998) = t;
    putf(ai + 0xa82c, F32(ADD(g_clock_game, 5.0f)));   /* 0x4a56d0 */
    put32(ai + 0x80, 0x420c0000);
    return 0;
}
/* tactic 2's enter 0x41c0c0: maybe the target; 1-4 s for t not a vehicle, else 3-7 */
static int war2_enter(Node *n, Node *t)
{
    uint8_t *ai = ai_of(n);
    maybe_target(ai, t);
    if (!vehicle(t)) after(ai, 4, 1.0f);   /* 0x4a5694 */
    else after(ai, 5, 3.0f);               /* 0x4a56f4 */
    put32(ai + 0x80, 0x420c0000);
    return 0;
}
/* tactic 3's enter 0x41c340: t the target, 25-39 s */
static int war3_enter(Node *n, Node *t)
{
    uint8_t *ai = ai_of(n);
    *Data_Ptr(ai, 0xa998) = t;
    after(ai, 15, 25.0f);   /* 0x4a56f8 */
    put32(ai + 0x80, 0x420c0000);
    return 0;
}
/* 0x41c1a0 (tactics 1-3's pop): on t's road (as the places stand), and t off the ground or the road going
   on from n's point */
static int war_over(Node *n, Node *t)
{
    uint8_t *ai = ai_of(n);
    const RoadPiece *p = *(const RoadPiece **)Data_Ptr(ai, 0xa96c);
    if (!p) return 0;
    const RoadPiece *tp = *(const RoadPiece **)Data_Ptr(ai_of(t), 0xa96c);
    if (!tp || tp->id != p->id) return 0;
    if (Road_Off(t)) return 1;
    int32_t i = rd32(ai + 0xa950);
    return Road_Next(&p, rd32(ai + 0xa954), &i) != 0;
}
/* 0x41c3c0 (tactic 3's pop): t under 5 m/s and within 30 m */
static int war3_over(Node *n, Node *t)
{
    if (rd32(t->data + 0xac) >= 0x40a00000) return 0;
    return Pair_Dist(n, t) < 30.0f;   /* 0x4a56a8 */
}

/* the offset Road_Chase reads from its unset local: the calls before it at its depth leave a pointer
   there (0x41c250's last argument when hanging back, 0x41c950's or 0x41c410's otherwise), as a float a
   tiny positive number; this one stands in for them */
#define LEFT_WORD 0x009c0000u
static float left_word(void) { float f; uint32_t b = LEFT_WORD; memcpy(&f, &b, 4); return f; }

/* tactic 0xa's enter 0x41c410: maybe the target, 1-4 s for t not a vehicle else 6-10 plus +0xa818; the
   place on the nearest road (+0xa960 cleared), the way toward the nearer end of its piece */
static int chase_enter(Node *n, Node *t)
{
    uint8_t *ai = ai_of(n);
    put32(ai + 0x80, 0x420c0000);
    maybe_target(ai, t);
    if (!vehicle(t)) after(ai, 4, 1.0f);
    else {
        float r = (float)(crt_rand() % 5);
        putf(ai + 0xa82c, F32(ADD(ADD(ADD(g_clock_game, r), (double)rd32(ai + 0xa818)), 6.0f)));   /* 0x4a56fc */
    }
    put32(ai + 0xa960, 0);
    int32_t i = rd32(ai + 0xa950);
    Roads_Nearest(F32(n->frame.pos[0]), F32(n->frame.pos[2]), (RoadPiece **)Data_Ptr(ai, 0xa96c), &i);
    put32(ai + 0xa950, i);
    const RoadPiece *p = *(const RoadPiece **)Data_Ptr(ai, 0xa96c);
    if (!p) {   /* the original reads through the null piece */
        plat_log("OpenI76: road chase without roads");
        return 0;
    }
    put32(ai + 0xa954, (double)i <= MUL((double)p->n, 0.5f) ? 1 : -1);   /* 0x4a5700 */
    return 0;
}
static int chase_frame(Node *n, Node *t) { Road_Chase(n, t, left_word()); return 0; }
/* 0x41c950 (tactic 0xa's pop): the road goes on and t is beyond 160 m */
static int chase_over(Node *n, Node *t)
{
    uint8_t *ai = ai_of(n);
    const RoadPiece *p = *(const RoadPiece **)Data_Ptr(ai, 0xa96c);
    int32_t i = rd32(ai + 0xa950);
    if (!Road_Next(&p, rd32(ai + 0xa954), &i)) return 0;
    return Pair_Dist(n, t) > 160.0;   /* 0x4a5720 */
}

/* hanging back's enter 0x4195f0: forward, mode 3, no target, 3-7 s; under 30 m from t chasing (+0xa9a0
   0), else either at random; +0xa99c-+0xa9b0 cleared and +0xa9a4 set (0x409a90, a partner, is always
   none) */
static int hang_enter(Node *n, Node *t)
{
    uint8_t *d = n->data, *ai = ai_of(n);
    put32(d + 0xf0, 0);
    put32(d + 0xe8, 0x3f800000);
    Vehicle_SetMode(n, 3);
    *Data_Ptr(ai, 0xa998) = NULL;
    after(ai, 5, 3.0f);   /* 0x4a55e8 */
    if (Pair_Dist(n, t) < 30.0f) put32(ai + 0xa9a0, 0);   /* 0x4a5598 */
    else put32(ai + 0xa9a0, crt_rand() % 2);
    put32(ai + 0xa99c, 0);
    put32(ai + 0xa9a4, 0);
    put32(ai + 0xa9ac, 0);
    put32(ai + 0xa9b0, 0);
    put32(ai + 0xa9a4, 1);   /* no partner */
    put32(d + 0xe8, 0x3f800000);
    Vehicle_SetMode(n, 3);
    return 0;
}
/* hanging back's frame 0x419730: under 50 m chasing, beyond 160 the road war, between them switching
   every 4-8 s; each on the road when on t's (0x41c250), else Combat's modes 4 and 2 */
static int hang_frame(Node *n, Node *t)
{
    uint8_t *ai = ai_of(n);
    float dist = Pair_Dist(n, t);
    if (dist < 50.0f) {   /* 0x4a5604 */
        put32(ai + 0xa9a0, 0);
        after(ai, 5, 4.0f);   /* 0x4a55b0 */
    } else if (fbits(dist) > 0x43200000) {   /* 160 */
        put32(ai + 0xa9a0, 1);
        after(ai, 5, 4.0f);
    } else if (!(g_clock_game < rdf(ai + 0xa82c))) {
        after(ai, 5, 4.0f);
        put32(ai + 0xa9a0, rd32(ai + 0xa9a0) == 0);
    }
    int32_t s = rd32(ai + 0xa9a0);
    if (s == 0) {
        if (Road_Together(n, t)) Road_Chase(n, t, left_word());
        else Combat(n, t, 4);
    } else if (s == 1) {
        if (Road_Together(n, t)) Road_War(n, t, 3);
        else Combat(n, t, 2);
    }
    return 0;
}
/* hanging back's leave 0x419910: t the target again */
static int hang_leave(Node *n, Node *t) { *Data_Ptr(ai_of(n), 0xa998) = t; return 0; }


/* ---- evade (0x14): driving to where t is ---- */

/* 0x419450: t too far: for class 9 beyond 720 m; t not a vehicle beyond 200 m; else 30 m above or below,
   or beyond 180 m in space */
static int far_off(Node *n, Node *t)
{
    if (n->cls == 9) return Pair_Dist(n, t) > 720.0f;   /* 0x4a55ec */
    if (!vehicle(t)) return Pair_Dist(n, t) > 200.0f;   /* 0x4a55f0 */
    if (fabs(SUB(n->frame.pos[1], t->frame.pos[1])) > 30.0) return 1;   /* 0x4a55f8 */
    return Pair_Dist3(n, t) > 180.0f;   /* 0x4a5600 */
}
/* 0x4195a0: near again: within 720 m in space for class 9, else 180 */
static int near_again(Node *n, Node *t) { return !(Pair_Dist3(n, t) > (n->cls == 9 ? 720.0f : 180.0f)); }
/* evade's enter 0x4194f0: the AI block's own path (+0xc: one point at +0x64, through +0x60) at t's place,
   followed as goto's (+0x8c, a search to start), the order's speed 35, forward, mode 3 */
static int evade_enter(Node *n, Node *t)
{
    uint8_t *d = n->data, *ai = ai_of(n);
    put32(d + 0xf0, 0);
    float p[3] = {F32(t->frame.pos[0]), F32(t->frame.pos[1]), F32(t->frame.pos[2])};
    memcpy(*Data_Ptr(ai, 0x60), p, sizeof p);
    uint32_t w = Fsm_Window(ai, 0xa9d8);   /* the block's words as the path's: +0x54 is +0x60's pointer */
    put32(ai + 0x60, (int32_t)(w + 0x64));
    put32(ai + 0x8c, (int32_t)(w + 0xc));
    put32(ai + 0x80, 0x420c0000);
    put32(ai + 0x84, 2);
    path_set(ai);
    put32(d + 0xe8, 0x3f800000);
    Vehicle_SetMode(n, 3);
    return 0;
}

/* ---- the fights by Combat's modes (4, 5, 8, 9, 0xe) and wandering (0xf) ---- */

/* 0x417ad0 (most tactics' first pop): hit this frame (+0xa6d8 is the AI's frame then, 0x40fae0) */
static int hit_now(Node *n, Node *t) { (void)t; return rd32(ai_of(n) + 0xa6d8) == g_ai_frames; }
/* 0x418260: the tactic's time (+0xa82c) is past */
static int timed_out(Node *n, Node *t) { (void)t; return g_clock_game > rdf(ai_of(n) + 0xa82c); }

/* the enters of 4, 5, 8 and 9 (0x417a40, 0x417f30, 0x417fe0, 0x417d00): a time rand() % m + base on (4:
   7 s flat), t the target, forward, mode 3, the order's speed 35 */
static void fight_enter(Node *n, Node *t, int m, float base)
{
    uint8_t *d = n->data, *ai = ai_of(n);
    if (m) after(ai, m, base);
    else putf(ai + 0xa82c, F32(ADD(g_clock_game, base)));
    *Data_Ptr(ai, 0xa998) = t;
    put32(d + 0xf0, 0);
    Vehicle_SetMode(n, 3);
    put32(d + 0xe8, 0x3f800000);
}
static int fight4_enter(Node *n, Node *t) { fight_enter(n, t, 0, 7.0f); put32(ai_of(n) + 0x80, 0x420c0000); return 0; }   /* 0x4a5560 */
static int fight5_enter(Node *n, Node *t) { fight_enter(n, t, 15, 20.0f); put32(ai_of(n) + 0x80, 0x420c0000); return 0; }   /* 0x4a556c */
static int fight8_enter(Node *n, Node *t) { fight_enter(n, t, 10, 10.0f); put32(ai_of(n) + 0x80, 0x420c0000); return 0; }   /* 0x4a5588 */
static int fight9_enter(Node *n, Node *t)
{
    fight_enter(n, t, 10, 7.0f);
    put32(ai_of(n) + 0xa97c, 1);   /* turning away first */
    put32(ai_of(n) + 0x80, 0x420c0000);
    return 0;
}
/* 9's frame 0x417da0: first (+0xa97c) away from t (its heading + 3.14, folded as 6.24 less it) while within
   70 m, opening up; then at t, stopping once facing it within 0.1 */
static int fight9_frame(Node *n, Node *t)
{
    uint8_t *ai = ai_of(n);
    float h = AI_Heading(n);
    (void)Pair_Angle(n, t);   /* asked for and dropped (it fills the cache) */
    float dist = Pair_Dist(n, t);
    if (rd32(ai + 0xa97c) && fbits(dist) < 0x428c0000) {   /* 70 */
        double ax = ADD(Pair_Angle(n, t), 3.140000104904175f);   /* 0x4a5574 */
        float a = F32(ax);
        if (ax > 3.140000104904175f) a = F32(SUB(6.239999771118164f, a));   /* 0x4a5578 */
        AI_Steer(n, a);
        open_up(n);
        return 0;
    }
    put32(ai + 0xa97c, 0);
    float a = Pair_Angle(n, t);
    AI_Steer(n, a);
    if (fabs(Angle_Diff(h, a)) < 0.10000000149011612) AI_Stop(n);   /* 0x4a5580 */
    else AI_Speed(n, AI_Cruise(n));
    return 0;
}
/* 0xe's enter 0x418090: maybe the target; 0.5 s for t not a vehicle, else 0.3 + 0.1 x +0xa818 */
static int fight_e_enter(Node *n, Node *t)
{
    uint8_t *d = n->data, *ai = ai_of(n);
    maybe_target(ai, t);
    if (!vehicle(t)) putf(ai + 0xa82c, F32(ADD(g_clock_game, 0.5f)));   /* 0x4a558c */
    else putf(ai + 0xa82c, F32(ADD(ADD(g_clock_game, MUL((double)rd32(ai + 0xa818), 0.10000000149011612f)), 0.30000001192092896f)));
    put32(d + 0xf0, 0);
    Vehicle_SetMode(n, 3);
    put32(d + 0xe8, 0x3f800000);
    put32(ai + 0x80, 0x420c0000);
    return 0;
}
/* wandering's enter 0x418190: 30-34 s, no target */
static int wander_enter(Node *n, Node *t)
{
    (void)t;
    uint8_t *d = n->data, *ai = ai_of(n);
    after(ai, 5, 30.0f);   /* 0x4a5598 */
    *Data_Ptr(ai, 0xa998) = NULL;
    put32(d + 0xf0, 0);
    Vehicle_SetMode(n, 3);
    put32(d + 0xe8, 0x3f800000);
    put32(ai + 0x80, 0x420c0000);
    return 0;
}
/* wandering's frame 0x418220 (also 0x1d's): 10 m/s, turning 1 rad off its heading */
static int wander_frame(Node *n, Node *t)
{
    (void)t;
    AI_Speed(n, 10.0f);
    AI_Steer(n, F32(ADD(AI_Heading(n), 1.0f)));   /* 0x4a5564 */
    return 0;
}

/* ---- backing down the road (0xb), the handbrake turn (0xc), backing into t (0xd) ---- */

/* 0xb's enter 0x419a20: t the target, 2-3 s, backward (mode 1), the order's speed 35; the place on the
   nearest road (+0xa960 cleared), the way toward the piece's farther half */
static int back_road_enter(Node *n, Node *t)
{
    uint8_t *d = n->data, *ai = ai_of(n);
    *Data_Ptr(ai, 0xa998) = t;
    after(ai, 2, 2.0f);   /* 0x4a55e4 */
    *Data_Ptr(ai, 0xa998) = t;
    put32(d + 0xf0, 0);
    put32(d + 0xe8, (int32_t)0xbf800000);
    Vehicle_SetMode(n, 1);
    put32(ai + 0x80, 0x420c0000);
    put32(ai + 0xa960, 0);
    int32_t i = rd32(ai + 0xa950);
    Roads_Nearest(F32(n->frame.pos[0]), F32(n->frame.pos[2]), (RoadPiece **)Data_Ptr(ai, 0xa96c), &i);   /* 0x41ac10 */
    put32(ai + 0xa950, i);
    const RoadPiece *p = *(const RoadPiece **)Data_Ptr(ai, 0xa96c);
    if (!p) {   /* the original reads through the null piece */
        plat_log("OpenI76: road chase without roads");
        return 0;
    }
    put32(ai + 0xa954, (double)i <= MUL((double)p->n, 0.5f) ? 1 : -1);   /* 0x4a558c */
    return 0;
}
/* 0xb's frame 0x419b20: Road_Chase (its unset local is the same kind of word) */
static int back_road_frame(Node *n, Node *t) { Road_Chase(n, t, left_word()); return 0; }
/* the leaves of 0xb and 0xd (0x419be0, 0x419fa0): forward, mode 3 */
static int forward_again(Node *n, Node *t)
{
    (void)t;
    put32(n->data + 0xe8, 0x3f800000);
    Vehicle_SetMode(n, 3);
    return 0;
}

/* 0xc's enter 0x419b40: 7 s, t the target, the heading axis noted (+0xa970), state 0 (+0xa97c), backward */
static int turn_enter(Node *n, Node *t)
{
    uint8_t *d = n->data, *ai = ai_of(n);
    putf(ai + 0xa82c, F32(ADD(g_clock_game, 7.0f)));   /* 0x4a5560 */
    *Data_Ptr(ai, 0xa998) = t;
    memcpy(ai + 0xa970, &n->frame.m[6], 12);
    put32(ai + 0xa97c, 0);
    put32(d + 0xf0, 0);
    put32(d + 0xe8, (int32_t)0xbf800000);
    Vehicle_SetMode(n, 1);
    return 0;
}
/* the noted axis against the node's z axis */
static double turned(uint8_t *ai, const Node *n, int order)
{
    float a0 = rdf(ai + 0xa970), a1 = rdf(ai + 0xa974), a2 = rdf(ai + 0xa978);
    const float *m = &n->frame.m[6];
    if (order == 0) return ADD(ADD(MUL(a0, m[0]), MUL(a2, m[2])), MUL(a1, m[1]));   /* 0x419c10 */
    return ADD(ADD(MUL(a2, m[2]), MUL(a0, m[0])), MUL(a1, m[1]));                   /* 0x419e20 */
}
/* 0xc's frame 0x419c10: 0 backing up to 14 m/s straight, then the axis noted, a full lock either way at
   random and the handbrake; 1 until under 1 m/s or turned past -0.4: the handbrake off; 2 stopping, then
   the steering mirrored and the direction flipped; 3 cruising */
static int turn_frame(Node *n, Node *t)
{
    (void)t;
    uint8_t *d = n->data, *ai = ai_of(n);
    int32_t s = rd32(ai + 0xa97c);
    switch (s) {
    case 0:
        if (rd32(d + 0xac) >= 0x41600000) {
            put32(ai + 0xa97c, 1);
            memcpy(ai + 0xa970, &n->frame.m[6], 12);
            int r = crt_rand();
            put32(d + 0xe0, 0x3f800000);
            if (r % 2 == 0) put32(d + 0xe0, (int32_t)0xbf800000);
            put32(d + 0xf0, 1);
            open_up(n);
        } else {
            put32(d + 0xe0, 0);   /* 0x40a3f0 */
            open_up(n);
        }
        break;
    case 1:
        if (rd32(d + 0xac) < 0x3f800000 || turned(ai, n, 0) < -0.4000000059604645f) {   /* 0x4a5610 */
            put32(ai + 0xa97c, 2);
            put32(d + 0xf0, 0);
        }
        break;
    case 2:
        if (rd32(d + 0xac) > 0x3f800000) {
            AI_Stop(n);
            break;
        }
        put32(ai + 0xa97c, 3);
        putf(d + 0xe0, -rdf(d + 0xe0));
        flip(n);
        break;
    case 3: AI_Speed(n, AI_Cruise(n)); break;
    default: plat_log(exe_str(0x4d24d8)); break;   /* "unkown turn state" (0x402c80) */
    }
    return 0;
}
/* 0xc's pop 0x419e20: the time is past, or turned past -0.85 */
static int turn_over(Node *n, Node *t)
{
    (void)t;
    uint8_t *ai = ai_of(n);
    if (g_clock_game > rdf(ai + 0xa82c)) return 1;
    return turned(ai, n, 1) < -0.8500000238418579f;   /* 0x4a5614 */
}
/* 0xc's leave 0x419e80: no handbrake, forward, mode 3 */
static int turn_leave(Node *n, Node *t)
{
    put32(n->data + 0xf0, 0);
    return forward_again(n, t);
}

/* 0xd's enter 0x419eb0: 5-8 s, t the target, backward, the order's speed 20 */
static int backing_into_enter(Node *n, Node *t)
{
    uint8_t *d = n->data, *ai = ai_of(n);
    after(ai, 4, 5.0f);   /* 0x4a55dc */
    *Data_Ptr(ai, 0xa998) = t;
    put32(d + 0xf0, 0);
    put32(d + 0xe8, (int32_t)0xbf800000);
    Vehicle_SetMode(n, 1);
    put32(ai + 0x80, 0x41a00000);
    return 0;
}

/* ---- sliding round (7), pushed from the fights ---- */

/* 0x41a580: at random (1 in 8 + 40 x (1 - the skill +0xa820)) against a vehicle, over 12 m/s, when the
   steering asks more sideways force than the four wheels' loads (+0xc) x 7.84 hold, past 13 */
static int slide_now(Node *n, Node *t)
{
    if (n->cls == 9 || !vehicle(t)) return 0;
    uint8_t *ai = ai_of(n);
    int r = crt_rand();
    int k = (int)MUL(SUB(1.0f, rdf(ai + 0xa820)), 40.0f);   /* 0x4a5618, __ftol */
    if (r % (k + 8) != 0) return 0;
    uint8_t *d = n->data;
    if (rd32(d + 0xac) < 0x41400000) return 0;   /* 12 */
    static const uint16_t w[4] = {0x3b8, 0x3a8, 0x3ac, 0x3bc};   /* 0x42d620, 0x42d600, 0x42d5f0, 0x42d610 */
    float load = rdf(((Node *)*Data_Ptr(d, w[0]))->data + 0xc);
    for (int i = 1; i < 4; i++) load = F32(ADD(rdf(((Node *)*Data_Ptr(d, w[i]))->data + 0xc), load));
    /* 0x475d00 (the ground's grip) is asked for and dropped */
    load = F32(MUL(load, 7.840000152587891f));   /* 0x4a5650 */
    float v = rdf(d + 0xac), st = rdf(d + 0xe0), m = rdf(d + 0x110), f;
    if (MUL(MUL(m, v), v) > load) f = F32(MUL(DIV(st, v), load));
    else f = F32(MUL(MUL(m, st), v));
    return fabs(MUL(f, v)) > 13.0f;   /* 0x4a5654 */
}
/* 7's enter 0x41a6f0: a full lock the way it steers, the handbrake, +0x9d34 in 0.5 s, +0xa97c set */
static int slide_enter(Node *n, Node *t)
{
    (void)t;
    uint8_t *d = n->data, *ai = ai_of(n);
    uint32_t s = (uint32_t)rd32(d + 0xe0);
    put32(d + 0xe0, s > 0x80000000u ? (int32_t)0xbf800000 : 0x3f800000);
    put32(d + 0xf0, 1);
    putf(ai + 0x9d34, F32(ADD(g_clock_game, 0.5f)));   /* 0x4a558c */
    put32(ai + 0xa97c, 1);
    return 0;
}
/* 7's frame 0x41a760: the handbrake held one frame, then off; opening up */
static int slide_frame(Node *n, Node *t)
{
    (void)t;
    uint8_t *ai = ai_of(n);
    if (rd32(ai + 0xa97c)) put32(ai + 0xa97c, 0);
    else put32(n->data + 0xf0, 0);
    open_up(n);
    return 0;
}
/* 0x41a7b0 (the fights' pop): t not a vehicle (its geometry's node, or the child's) within its radius +
   35 m */
static int near_object(Node *n, Node *t)
{
    if (vehicle(t) || !t) return 0;
    if (!t->geom) t = t->child;
    if (!t) return 0;
    float r = F32(ADD(t->radius, 35.0f));   /* 0x4a5568 */
    return Pair_Dist(n, t) < r;
}

/* ---- driving the path in company (0x19, 0x1b's frame) ---- */

/* 0x409aa0: no vehicle of team 0 has its place dotted with p (a dot product, not a distance: the
   original's) under 6400 */
static int clear_of_team0(const float p[3])
{
    for (int i = 0; i < g_team_count[0] && i < 0x40; i++) {
        const double *q = g_teams[0][i]->node->frame.pos;
        double s = ADD(ADD(MUL(q[0], p[0]), MUL(q[1], p[1])), MUL(q[2], p[2]));
        if (s < 6400.0f) return 0;   /* 0x4a5184 */
    }
    return 1;
}
/* 0x19's enter 0x41d730 (also 0x1e's 0x41a070): no handbrake, forward, mode 3 */
static int company_enter(Node *n, Node *t)
{
    (void)t;
    put32(n->data + 0xf0, 0);
    put32(n->data + 0xe8, 0x3f800000);
    Vehicle_SetMode(n, 3);
    return 0;
}
/* the frames of 0x19 (0x41d760) and 0x1b (0x41d2e0): along the path; every 2-5 s the side (+0x9cfc)
   flips. Within 90 m of t and clear of team 0 (and for 0x1b, +0xa6f0 within 20 s) it aims 22 m on at its
   side, else 11 m on its line; at the order's speed, cruising when steering past 0.6 (on its line 0.4,
   0x1b 0.5) */
static void company_frame(Node *n, Node *t, int race)
{
    uint8_t *ai = ai_of(n);
    Path_Advance(ai);
    float now = g_clock_game;
    if (now > rdf(ai + 0x9cf8)) {
        float r = (float)(crt_rand() % 4);
        putf(ai + 0x9cf8, F32(ADD(ADD(r, now), 2.0f)));   /* 0x4a5734 */
        put32(ai + 0x9cfc, rd32(ai + 0x9cfc) == 0);
    }
    float p[3] = {F32(n->frame.pos[0]), F32(n->frame.pos[1]), F32(n->frame.pos[2])};
    float dx = F32(SUB(t->frame.pos[0], p[0])), dy = F32(SUB(t->frame.pos[1], p[1])), dz = F32(SUB(t->frame.pos[2], p[2]));
    double s = ADD(ADD(MUL(dy, dy), MUL(dz, dz)), MUL(dx, dx));
    bool company = s < 8100.0f && clear_of_team0(p);   /* 0x4a5738 */
    if (company && race) company = ADD(rdf(ai + 0xa6f0), 20.0f) > now;   /* 0x4a573c */
    int side = company ? rd32(ai + 0x9cfc) : 0;
    AI_Steer(n, Path_Aim(n, ai, side, company ? 22.0f : 11.0f));
    float want = rdf(ai + 0x80);
    if (n->cls != 9) {
        double lim = !company ? (race ? 0.5 : 0.4000000059604645) : 0.6000000238418579;   /* 0x4a5748, 0x4a5768, 0x4a5750 */
        if (fabs(rdf(n->data + 0xe0)) > lim) want = AI_Cruise(n);
    }
    AI_Speed(n, want);
}

/* ---- racing (0x1a) ---- */

static float fsm_f(uint32_t a) { int32_t w = Fsm_Word(a); float f; memcpy(&f, &w, 4); return f; }

/* 0x41d0a0: the race's point (+0xa0): the path's nearest point over the ground, then on while the next
   one does not lie behind (Dir_Cos above -0.1, as bits), round the loop at most once */
static void race_start(Node *n)
{
    uint8_t *ai = ai_of(n);
    const Frame *f = *(const Frame **)Data_Ptr(ai, 0);
    float x = F32(f->pos[0]), z = F32(f->pos[2]);
    uint32_t path = (uint32_t)rd32(ai + 0x8c);
    int32_t count = Fsm_Word(path + 0x50), best = rd32(ai + 0xa0);   /* unset in the original (a word of its stack) when no point is nearer */
    float bd = 3.4028234663852886e+38f;
    uint32_t pts = (uint32_t)Fsm_Word(path + 0x54);
    for (int32_t i = 0; i < count; i++) {
        double dx = SUB(x, fsm_f(pts + (uint32_t)i * 12)), dz = SUB(z, fsm_f(pts + (uint32_t)i * 12 + 8));
        double d = ADD(MUL(dx, dx), MUL(dz, dz));
        if (d < bd) best = i, bd = F32(d);
    }
    int32_t i = best;
    for (;;) {
        uint32_t a = pts + (uint32_t)i * 12, b = i < count - 1 ? a + 12 : pts;
        float c = F32(Dir_Cos(fsm_f(a), fsm_f(a + 8), fsm_f(b), fsm_f(b + 8), x, z));
        uint32_t cb;
        memcpy(&cb, &c, 4);
        if (cb >= 0xbdcccccdu) break;   /* -0.1 or below */
        if (++i >= count) i = 0;
        if (i == best) break;
    }
    put32(ai + 0xa0, i);
}
/* race's enter 0x41d240: the script's path followed as goto's (a search to start), forward, mode 3; from
   the race's start when it has not looped (+0x9c) */
static int race_enter(Node *n, Node *t)
{
    (void)t;
    uint8_t *d = n->data, *ai = ai_of(n);
    int32_t looped = rd32(ai + 0x9c);
    path_set(ai);
    put32(ai + 0x84, 2);
    put32(ai + 0x9c, 0);
    put32(d + 0xe8, 0x3f800000);
    Vehicle_SetMode(n, 3);
    put32(d + 0xf0, 0);
    put32(d + 0xe8, 0x3f800000);
    Vehicle_SetMode(n, 3);
    if (!looped) race_start(n);
    return 0;
}

/* ---- driving in formation (0x1e) ---- */

/* 0x1e's frame 0x41a0a0: toward its place in t's frame (+0xa980 across, +0xa984 along; +0xa988 set within
   2.5 m). With t under 1 m/s: cruising when steering right past 0.4, stopping within 4 m, else (d - 1) x
   0.3. With t moving and steering under 0.4: flat out beyond 150 m, t's speed + (d - 2) x 0.7 beyond 50,
   else + (d - 1) x 0.4. Steering harder: within 100 m of it and t over 5 m/s, when the place is behind and
   both face alike, aiming 4 x d along t's axis past the place at 0.75 of t's speed; else t's speed - 5
   while they still close (0x4139f0), cruising otherwise */
static int formation_frame(Node *n, Node *t)
{
    uint8_t *d = n->data, *ai = ai_of(n);
    float h = AI_Heading(n);
    (void)AI_Heading(t);   /* asked for and dropped */
    float vn[3], sn, vt[3], st;
    Class_Velocity(n, vn, &sn);
    Class_Velocity(t, vt, &st);
    float in[1][3] = {{rdf(ai + 0xa980), 0.0f, rdf(ai + 0xa984)}}, out[1][3];
    Frame_Points(out, (const float(*)[3])in, 1, &t->frame);   /* 0x452740 */
    const float *p = out[0];
    float q[3] = {F32(n->frame.pos[0]), F32(n->frame.pos[1]), F32(n->frame.pos[2])};
    float dx = F32(SUB(p[0], q[0])), dy = F32(SUB(p[1], q[1]));
    double dzx = SUB(p[2], q[2]);
    float dz = F32(dzx);
    double dist = fpu_sqrt(ADD(ADD(MUL(dzx, dz), MUL(dy, dy)), MUL(dx, dx)));
    float df = F32(dist);
    put32(ai + 0xa988, !(dist >= 2.5f));   /* 0x4a561c */
    float a = fbits(df) < 0x358637bd ? h : F32(atan2(dx, dz));
    AI_Steer(n, a);
    (void)Angle_Diff(h, a);   /* asked for and dropped */
    if (fbits(st) < 0x3f800000) {
        if (rd32(d + 0xe0) >= 0x3ecccccd) AI_Speed(n, AI_Cruise(n));
        else if (fbits(df) < 0x40800000) AI_Stop(n);
        else {
            double s = MUL(SUB(df, 1.0f), 0.30000001192092896f);   /* 0x4a5594 */
            AI_Speed(n, F32(0.0 < s ? s : 0.0));   /* 0x4a559c */
        }
        return 0;
    }
    if (fabs(rdf(d + 0xe0)) < 0.4000000059604645) {   /* 0x4a5628 */
        if (fbits(df) > 0x43160000) AI_Speed(n, 1000.0f);
        else if (fbits(df) > 0x42480000) {
            double s = MUL(SUB(df, 2.0f), 0.699999988079071f);   /* 0x4a5640 */
            AI_Speed(n, F32(ADD(0.0 < s ? s : 0.0, st)));
        } else {
            double s = SUB(df, 1.0f);
            AI_Speed(n, F32(ADD(MUL(0.0 < s ? s : 0.0, 0.4000000059604645f), st)));   /* 0x4a5624 */
        }
        return 0;
    }
    if (fbits(df) >= 0x42c80000 || fbits(st) <= 0x40a00000) {   /* 100, 5 */
        AI_Speed(n, AI_Cruise(n));
        return 0;
    }
    const float *nz = &n->frame.m[6], *tz = &t->frame.m[6];
    float dot1 = F32(ADD(ADD(MUL(dy, nz[1]), MUL(dz, nz[2])), MUL(dx, nz[0])));
    double dot2 = ADD(ADD(MUL(tz[1], nz[1]), MUL(tz[2], nz[2])), MUL(tz[0], nz[0]));
    if (dot2 > 0.699999988079071f && (uint32_t)fbits(dot1) > 0x80000000u) {
        double x = ADD(MUL(MUL(df, tz[0]), 4.0f), p[0]);   /* 0x4a55b0 */
        float zs = F32(MUL(MUL(tz[2], df), 4.0f));
        float ex = F32(SUB(x, q[0])), ez = F32(SUB(ADD(p[2], zs), q[2]));
        AI_Steer(n, F32(atan2(ex, ez)));
        AI_Speed(n, F32(MUL(st, 0.75f)));   /* 0x4a5648 */
        return 0;
    }
    if (AI_Closest(q, vn, p, vt) > 0.0f) AI_Speed(n, F32(SUB(st, 5.0f)));   /* 0x4a55dc */
    else AI_Speed(n, AI_Cruise(n));
    return 0;
}

int AI_TacticCall(uint32_t va, Node *n, Node *t)
{
    switch (va) {
    case 0: return 0;
    case 0x417a10: return 1;   /* any target will do */
    case 0x40a580: return sit_frame(n, t);
    case 0x40a600: return hide_enter(n, t);
    case 0x40a620: return hide_leave(n, t);
    case 0x4183a0: return rolling_back(n, t);
    case 0x418400: return not_rolling_back(n, t);
    case 0x418460: return back_enter(n, t);
    case 0x4184b0: return back_frame(n, t);
    case 0x4189c0: return stuck(n, t);
    case 0x41c060: return war_enter(n, t);
    case 0x41c0a0: Road_War(n, t, 0); return 0;
    case 0x41c0c0: return war2_enter(n, t);
    case 0x41c180: Road_War(n, t, 1); return 0;
    case 0x41c340: return war3_enter(n, t);
    case 0x41c3a0: Road_War(n, t, 3); return 0;
    case 0x41c1a0: return war_over(n, t);
    case 0x41c3c0: return war3_over(n, t);
    case 0x41c410: return chase_enter(n, t);
    case 0x41c6b0: return chase_frame(n, t);
    case 0x41c950: return chase_over(n, t);
    case 0x4198c0: return crowded(n, t);
    case 0x419930: return AI_TargetLost(n, t);
    case 0x4199a0: return AI_TargetSeen(n, t);
    case 0x4198f0: return room_again(n, t);
    case 0x4195f0: return hang_enter(n, t);
    case 0x419730: return hang_frame(n, t);
    case 0x419910: return hang_leave(n, t);
    case 0x417ad0: return hit_now(n, t);
    case 0x418260: return timed_out(n, t);
    case 0x417a40: return fight4_enter(n, t);
    case 0x417ab0: Combat(n, t, 0); return 0;
    case 0x417f30: return fight5_enter(n, t);
    case 0x417fc0: Combat(n, t, 3); return 0;
    case 0x417fe0: return fight8_enter(n, t);
    case 0x418070: Combat(n, t, 2); return 0;
    case 0x417d00: return fight9_enter(n, t);
    case 0x417da0: return fight9_frame(n, t);
    case 0x418090: return fight_e_enter(n, t);
    case 0x418170: Combat(n, t, 4); return 0;
    case 0x418190: return wander_enter(n, t);
    case 0x418220: return wander_frame(n, t);
    case 0x419a20: return back_road_enter(n, t);
    case 0x419b20: return back_road_frame(n, t);
    case 0x419be0: return forward_again(n, t);
    case 0x419b40: return turn_enter(n, t);
    case 0x419c10: return turn_frame(n, t);
    case 0x419e20: return turn_over(n, t);
    case 0x419e80: return turn_leave(n, t);
    case 0x419eb0: return backing_into_enter(n, t);
    case 0x419fa0: return forward_again(n, t);
    case 0x41a580: return slide_now(n, t);
    case 0x41a6f0: return slide_enter(n, t);
    case 0x41a760: return slide_frame(n, t);
    case 0x41a7b0: return near_object(n, t);
    case 0x41d730: return company_enter(n, t);
    case 0x41d240: return race_enter(n, t);
    case 0x41a070: return company_enter(n, t);
    case 0x41a0a0: return formation_frame(n, t);
    case 0x41d760: company_frame(n, t, 0); return 0;
    case 0x41d2e0: company_frame(n, t, 1); return 0;
    case 0x419450: return far_off(n, t);
    case 0x4195a0: return near_again(n, t);
    case 0x4194f0: return evade_enter(n, t);
    case 0x417b00: return attack_enter(n, t);
    case 0x419070: return avoid_enter(n, t);
    case 0x419210: return avoid_frame(n, t);
    case 0x419410: return avoid_leave(n, t);
    case 0x418290: return looked(n, t);
    case 0x418df0: return ahead_not_target(n, t);
    case 0x418e70: return ahead(n, t);
    case 0x418ee0: return ahead_driving(n, t);
    case 0x418a50: return blocked(n, t);
    case 0x418500: return stuck_enter(n, t);
    case 0x418660: return stuck_frame(n, t);
    case 0x418ad0: return stuck_leave(n, t);
    case 0x418850: return unstuck(n, t);
    case 0x418b60: return wall_ahead(n, t);
    case 0x418ce0: return backing_enter(n, t);
    case 0x418d70: return backing_frame(n, t);
    case 0x418d80: return backing_leave(n, t);
    case 0x417c00: return attack_frame(n, t);
    case 0x41d1c0: return goto_enter(n, t);
    case 0x41d4a0: return goto_frame(n, t);
    case 0x41d5d0: return searched(n, t);
    case 0x41d5f0: return path_changed(n, t);
    case 0x41d620: return follow_enter(n, t);
    case 0x41d650: return follow_frame(n, t);
    case 0x41d6f0: return follow_done(n, t);
    default: {
        static uint32_t told[128];
        static int ntold;
        int seen = 0;
        for (int i = 0; i < ntold; i++) seen |= told[i] == va;
        if (!seen && ntold < 128) {
            told[ntold++] = va;
            char m[80];
            snprintf(m, sizeof m, "OpenI76: the AI's tactic function 0x%x is not ported yet", va);
            plat_log(m);
        }
        return 0;
    }
    }
}

/* ---- the orders ---- */

#define TACTIC(i, off) exe_u32(0x4a9400 + (uint32_t)(i) * 0x1354 + (off))

static int generic(int order) { return order >= 0 && order <= 0xf && order != 7; }

/* 0x40f410: each of the block's weapons fired at t when the decision says so (0x413b10 with its muzzle's
   frame, 0x4551d0; its trigger 0x455350) */
static void aim(Node *n, uint8_t *ai, Node *t)
{
    if (!t) return;   /* the original reads through it: following without a leader does not happen */
    for (int i = 0; i < rd32(ai + 0xa718); i++) {
        int32_t inst = rd32(ai + 0xa71c + i * 4), fact[8];
        WeaponInst *w = &g_weapons[inst];
        if (w->hp->vehicle != n) plat_log(exe_str(0x4a92a8));   /* 0x458980: "weapons index out of order" */
        if (w->index != inst) plat_log("OpenI76: a freed weapon's frame (0x4551d0 leaves it unset)");
        Frame mz;
        Node_FrameIn(&mz, w->muzzle, NULL);
        memcpy(fact, ai + 0xa738 + i * 0x20, sizeof fact);
        if (AI_ShouldFire(n, t, rdf(ai + 0xa828), rdf(ai + 0xa824), fact, &mz)) w->v44 = 1;
    }
}

void AI_Order(Node *n, Node *t, int order, int slow)
{
    (void)slow;
    if (!n) return;
    uint8_t *ai = ai_of(n);
    Node **target = (Node **)Data_Ptr(ai, 0xa9b4);
    if (t != *target) {
        if (*target) Node_Unref(*target);   /* tagged "ai" */
        if (t) Node_Ref(t);
        *target = t;
    }
    if (t) {
        *target = Node_Live(t);
        if (!*target) {
            AI_Stop(n);
            return;
        }
    }
    /* 0x417a10 takes any target */
    Weapons_Refresh(n);
    int32_t top;
    memcpy(&top, ai + 0xa93c, 4);
    int32_t bottom;
    memcpy(&bottom, ai + 0xa874, 4);
    bool fresh;
    if (top == -1 || AI_UserInScene(n)) fresh = true;   /* 0x409b80 */
    else if (generic(order)) fresh = !generic(bottom);
    else fresh = generic(bottom) || order != bottom;
    Node **last = (Node **)Data_Ptr(ai, 0xa944);
    int32_t tac;
    if (fresh || *last != t) {
        *last = t;
        tac = generic(order) ? 6 : order;   /* 0x40cc00 */
        put32(ai + 0xa93c, 0);
        put32(ai + 0xa874, tac);
        AI_TacticCall(TACTIC(tac, 8), n, t);
    } else {
        /* from the top down, the first tactic with a pop that holds is left */
        for (;;) {
            memcpy(&top, ai + 0xa93c, 4);
            int32_t cur;
            memcpy(&cur, ai + 0xa874 + (uint32_t)top * 4, 4);
            tac = cur;
            int32_t pops = (int32_t)TACTIC(cur, 0x8c);
            if (pops < 1) break;
            int k = 0;
            while (!AI_TacticCall(TACTIC(cur, 0x90 + (uint32_t)k * 0x1d8), n, t))
                if (++k >= pops) goto frame;
            AI_TacticCall(TACTIC(cur, 0x1300), n, t);
            memcpy(&top, ai + 0xa93c, 4);
            if (top < 1) {
                tac = generic(bottom) ? 6 : order;   /* 0x40cc00 with the tactic and the pop's number */
                put32(ai + 0xa874 + (uint32_t)top * 4, tac);
                AI_TacticCall(TACTIC(tac, 8), n, t);
                break;
            }
            put32(ai + 0xa93c, top - 1);
        }
    }
frame:
    AI_TacticCall(TACTIC(tac, 4), n, t);
    for (;;) {
        int32_t pushes = (int32_t)TACTIC(tac, 0x38);
        if (pushes < 1) break;
        int k = 0;
        while (!AI_TacticCall(TACTIC(tac, 0x3c + (uint32_t)k * 8), n, t))
            if (++k >= pushes) goto done;
        tac = (int32_t)TACTIC(tac, 0x40 + (uint32_t)k * 8);
        memcpy(&top, ai + 0xa93c, 4);
        if (top < 0x31) {
            put32(ai + 0xa93c, top + 1);
            put32(ai + 0xa874 + (uint32_t)(top + 1) * 4, tac);
            AI_TacticCall(TACTIC(tac, 8), n, t);
            AI_TacticCall(TACTIC(tac, 4), n, t);
        } else
            plat_log(exe_str(0x4a9224));   /* "Ptable stack overflow", the original's fatal */
    }
done:
    if (t != n) {
        if (order == 0x1e) {
            if (rd32(ai + 0xa98c)) aim(n, ai, *(Node **)Data_Ptr(ai, 0xa990));
        } else if (t)
            aim(n, ai, t);
    }
}
