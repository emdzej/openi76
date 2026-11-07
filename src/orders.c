/* The AI's orders and tactics (orders.h). In the current rounding mode (render/fpu.h). */
#include "orders.h"
#include "ai.h"
#include "damage.h"
#include "drive.h"
#include "entity.h"
#include "exe.h"
#include "fsm.h"
#include "nav.h"
#include "physics.h"
#include "platform.h"
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
static int call(uint32_t va, Node *n, Node *t)
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

/* 0x408c40: the weapons' list (+0xa718, from 0x454f80 / 0x455080, then 0x4089d0) while +0xa714 is set;
   not ported yet */
static void weapons_refresh(Node *n) { (void)n; }
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
    weapons_refresh(n);
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
        call(TACTIC(tac, 8), n, t);
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
            while (!call(TACTIC(cur, 0x90 + (uint32_t)k * 0x1d8), n, t))
                if (++k >= pops) goto frame;
            call(TACTIC(cur, 0x1300), n, t);
            memcpy(&top, ai + 0xa93c, 4);
            if (top < 1) {
                tac = generic(bottom) ? 6 : order;   /* 0x40cc00 with the tactic and the pop's number */
                put32(ai + 0xa874 + (uint32_t)top * 4, tac);
                call(TACTIC(tac, 8), n, t);
                break;
            }
            put32(ai + 0xa93c, top - 1);
        }
    }
frame:
    call(TACTIC(tac, 4), n, t);
    for (;;) {
        int32_t pushes = (int32_t)TACTIC(tac, 0x38);
        if (pushes < 1) break;
        int k = 0;
        while (!call(TACTIC(tac, 0x3c + (uint32_t)k * 8), n, t))
            if (++k >= pushes) goto done;
        tac = (int32_t)TACTIC(tac, 0x40 + (uint32_t)k * 8);
        memcpy(&top, ai + 0xa93c, 4);
        if (top < 0x31) {
            put32(ai + 0xa93c, top + 1);
            put32(ai + 0xa874 + (uint32_t)(top + 1) * 4, tac);
            call(TACTIC(tac, 8), n, t);
            call(TACTIC(tac, 4), n, t);
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
