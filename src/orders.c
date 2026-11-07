/* The AI's orders and tactics (orders.h). In the current rounding mode (render/fpu.h). */
#include "orders.h"
#include "ai.h"
#include "avoid.h"
#include "clock.h"
#include "combat.h"
#include "crt.h"
#include "damage.h"
#include "drive.h"
#include "entity.h"
#include "exe.h"
#include "fsm.h"
#include "nav.h"
#include "physics.h"
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
static bool below(double a, double b) { return !(a >= b); }   /* x87: unordered counts */
static bool below_eq_d(double a, double b) { return !(a > b); }
static uint8_t *ai_of(Node *n) { return *Data_Ptr(n->data, 0x108); }   /* 0x42d380 */

/* ---- small vehicle steps ---- */

void Vehicle_SetMode(Node *n, int32_t mode)
{
    if (n->cls == 1) put32(n->data + 0x104, mode);
}

/* 0x4683c0: a sound at the node; no audio yet */
static void sound(int id, Node *n) { (void)id, (void)n; }

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
   a car ahead sounds the horn one time in four, 0x476440) */
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
    g_way_words[0] = 5, g_way_words[1] = 8;   /* the horn's calls (not ported) would write over them */
    if (r && *who && ((*who)->cls == 1 || (*who)->cls == 8 || (*who)->cls == 9)) {
        int k = crt_rand();
        if ((k & 3) == 0) {
            static int told;
            if (!told++) plat_log("OpenI76: the horn (0x476440) is not ported yet");
        }
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
    putf(ai + 0x9d38, AI_Heading(n));   /* 0x4182c0 */
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
    putf(ai + 0x9d38, AI_Heading(n));
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
    float h = AI_Heading(n);
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
    float h = AI_Heading(n);
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

/* 0x40f410: the aim at the target; not ported yet */
static void aim(Node *n, uint8_t *ai, Node *t) { (void)n, (void)ai, (void)t; }

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
