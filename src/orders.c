/* The AI's orders and tactics (orders.h). In the current rounding mode (render/fpu.h). */
#include "orders.h"
#include "ai.h"
#include "damage.h"
#include "entity.h"
#include "exe.h"
#include "physics.h"
#include "platform.h"
#include "render/fpu.h"
#include <stdio.h>
#include <string.h>

Node *g_ai_user;

static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }
static void put32(uint8_t *p, int32_t v) { memcpy(p, &v, 4); }
static void putf(uint8_t *p, float v) { memcpy(p, &v, 4); }
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

/* a tactic's function by its address */
static int call(uint32_t va, Node *n, Node *t)
{
    switch (va) {
    case 0: return 0;
    case 0x417a10: return 1;   /* any target will do */
    case 0x40a580: return sit_frame(n, t);
    case 0x40a600: return hide_enter(n, t);
    case 0x40a620: return hide_leave(n, t);
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
