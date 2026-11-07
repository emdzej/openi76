/* The AI (ai.h). In the current rounding mode (render/fpu.h). */
#include "ai.h"
#include "entity.h"
#include "weapons.h"
#include "damage.h"
#include "clock.h"
#include "combat.h"
#include "nav.h"
#include "avoid.h"
#include "fsm.h"
#include "orders.h"
#include "terrain.h"
#include "render/fpu.h"
#include <string.h>

#define ADD fpu_add
#define SUB fpu_sub
#define MUL fpu_mul
#define DIV fpu_div
#define F32 fpu_f32

int g_team_count[AI_TEAMS];
Entity *g_teams[AI_TEAMS][0x40];

static float rdf(const uint8_t *p) { float v; memcpy(&v, p, 4); return v; }
static void put32(uint8_t *p, int32_t v) { memcpy(p, &v, 4); }
static void putf(uint8_t *p, float v) { memcpy(p, &v, 4); }
static int below(double a, double b) { return a < b || a != a || b != b; }

static uint8_t *ai_of(Node *vehicle) { return *Data_Ptr(vehicle->data, 0x108); }   /* 0x42d380 */

/* a wheel's data (the vehicle data's slot), 0x42d5f0-0x42d620 */
static const uint8_t *wheel(const uint8_t *d, int off) { return ((const Node *)*Data_Ptr((uint8_t *)d, off))->data; }

/* 0x409be0: from the four wheels' +0xc and the vehicle's 0x110, the AI's 0xa994 */
static void ai_wheels(Node *vehicle)
{
    uint8_t *d = vehicle->data, *ai = ai_of(vehicle);
    float s = F32(ADD(rdf(wheel(d, 0x3ac) + 0xc), rdf(wheel(d, 0x3a8) + 0xc)));
    s = F32(ADD(rdf(wheel(d, 0x3b8) + 0xc), s));
    double t = MUL(ADD(rdf(wheel(d, 0x3bc) + 0xc), s), 7.84f);   /* 0x4a51b0 */
    if (below(t, 0.0f)) {                                         /* 0x4a5188 */
        putf(ai + 0xa994, 1.0f);
        return;
    }
    putf(ai + 0xa994, F32(fpu_sqrt(DIV(F32(t), rdf(d + 0x110)))));
}

/* 0x408510: a vehicle (not an artillery gun) joins its team (the node's flags' bits 16-18) */
static void ai_add(Entity *e)
{
    Node *n = e->node;
    if (n->cls == 8 || !(n->cls == 1 || n->cls == 8 || n->cls == 9)) return;   /* 0x42e570 */
    int team = (int)((n->flags & 0x70000) >> 16);
    /* the original looks for the entity in its team first, and adds it whatever it finds */
    int k = g_team_count[team];
    if (k < 0x40) g_teams[team][k] = e;   /* the original's lists are 0x40 long */
    g_team_count[team] = k + 1;
    uint8_t *d = n->data, *ai = ai_of(n);
    *Data_Ptr(ai, 0) = &n->frame;
    if (n->cls == 9) {
        put32(ai + 0x9d14, 0);
    } else {
        put32(ai + 0x9d14, 1);
        ai_wheels(n);
    }
    /* 0x40cbb0: the tactics table's tags checked (0x4a9400, a fatal if one is not its index); the
       tactics' state */
    put32(ai + 0xa82c + 0x110, -1);
    put32(ai + 0xa82c + 0x11c, 1);
    put32(d + 0x45c, Terrain_Class(n->frame.pos[0], n->frame.pos[2]));
    AI_Rethink();   /* 0x408bf0, inlined here */
    put32(ai + 8, 1);
}

void AI_Rethink(void)
{
    for (int t = 0; t < AI_TEAMS; t++)
        for (int i = 0; i < g_team_count[t] && i < 0x40; i++) put32(ai_of(g_teams[t][i]->node) + 0xa714, 1);
}

int g_user_pressed;

static int32_t g_ai_505458, g_ai_50545c, g_ai_50544c, g_ai_505448;   /* the user's hiding, AI_Frame's end */

void AI_Init(void)
{
    memset(g_team_count, 0, sizeof g_team_count);
    memset(g_teams, 0, sizeof g_teams);
    Entity_OnAdd(ai_add);
    g_ai_505458 = g_ai_50545c = g_ai_50544c = g_ai_505448 = 0;
    Nav_Init();   /* 0x40fda0 */
    /* the removal callback 0x408660 and the rest of the set-up (0x40fac0, 0x40f0b0, 0x40d0b0,
       0x40b7f0, the skill tables 0x40b380) come with the driving */
}

int g_user_pressed_prev;

bool AI_UserInScene(Node *n)
{
    return g_user_entity && g_user_entity->node == n && !g_user_pressed_prev;
}

void AI_Begin(void)
{
    g_ai_user = g_user_entity ? g_user_entity->node : NULL;
    g_user_pressed = g_user_pressed_prev = 0;
}

/* counters 0x40f0a0, 0x40f050 and 0x40f060 keep each frame; nothing else reads them */
static int32_t g_ai_505514, g_ai_505508, g_ai_50550c = 1, g_ai_505510 = 1;   /* as 0x40f010 sets them */

/* the look-ahead box of each team's vehicles (in 0x409020): from the centre along its z axis laid on the
   ground (n x (z x n), n the ground's normal there) for 50 x the frame rate (at most 10000) x the
   direction +0xe8, 3 m wider all round: +0x9d40 / +0x9d4c the x range, +0x9d48 / +0x9d54 the z range */
static void ai_boxes(void)
{
    for (int t = 0; t < AI_TEAMS; t++)
        for (int i = 0; i < g_team_count[t]; i++) {
            Node *n = g_teams[t][i]->node;
            uint8_t *d = n->data;
            float c[4], nr[3];
            Node_Centre(c, n);   /* 0x4223e0 */
            Terrain_Normal(nr, c[0], c[2]);
            const float *z = &n->frame.m[6];
            float u0 = F32(SUB(MUL(z[2], nr[1]), MUL(z[1], nr[2])));
            float u1 = F32(SUB(MUL(z[0], nr[2]), MUL(z[2], nr[0])));
            float u2 = F32(SUB(MUL(z[1], nr[0]), MUL(z[0], nr[1])));
            float v0 = -u0, v1 = -u1, v2 = -u2;   /* z x n */
            float w0 = F32(SUB(MUL(v2, nr[1]), MUL(v1, nr[2])));
            float w1 = F32(SUB(MUL(v0, nr[2]), MUL(v2, nr[0])));
            float w2 = F32(SUB(MUL(v1, nr[0]), MUL(v0, nr[1])));
            (void)w1;
            float fps = g_clock_inv > 10000.0f ? 10000.0f : g_clock_inv;   /* 0x4a5178 */
            double k = MUL(MUL(fps, 50.0f), rdf(d + 0xe8));                 /* 0x4a517c */
            float x = F32(ADD(MUL(w0, k), c[0]));
            float z2 = F32(ADD(c[2], F32(MUL(k, w2))));
            uint8_t *ai = ai_of(n);
            float lo = c[0] < x ? c[0] : x, hi = c[0] < x ? x : c[0];
            putf(ai + 0x9d40, F32(SUB(lo, 3.0f)));   /* 0x4a5180 */
            putf(ai + 0x9d4c, F32(ADD(hi, 3.0f)));
            lo = c[2] < z2 ? c[2] : z2, hi = c[2] < z2 ? z2 : c[2];
            putf(ai + 0x9d48, F32(SUB(lo, 3.0f)));
            putf(ai + 0x9d54, F32(ADD(hi, 3.0f)));
        }
}

void AI_Frame(void)
{
    g_user_pressed_prev = g_user_pressed;   /* 0x409b50 */
    g_user_pressed = 0;
    g_ai_505514 = 0;   /* 0x40f0a0 */
    g_ai_505508 = 0;   /* 0x40f050 */
    ai_boxes();
    Fsm_Frame();   /* outside the melee (0x407370) and while 0x505450 is clear */
    g_ai_505510 = g_ai_505508 < g_ai_50550c ? 1 : g_ai_505508 == g_ai_50550c ? 0 : 2;   /* 0x40f060 */
    /* the user hidden from the radar (0x421e30, for its side effects: the jammer's time up switches it
       off) outside network play; the words it clears are not read anywhere. The original asks between
       0x40fad0's count and the search's turns, which do not depend on it */
    if (!g_net_game) g_ai_505458 = g_user_entity ? Radar_Hidden(g_user_entity->node) : 0;
    if (g_ai_505458) {
        if (!g_ai_50545c) g_ai_50544c = 0;
        g_ai_50545c = 0;
        g_ai_505448 = 0;
    }
    AI_SearchTurns();
}

/* 0x4099e0: one line of sight a frame while the user hides (the first ask since the reset, and then each
   time the asks so far match the grants); the count of asks goes up either way */
static int sight_turn(void)
{
    if (!g_ai_50545c && g_ai_505448 == g_ai_50544c) {
        g_ai_50544c++;
        g_ai_50545c = 1;
        g_ai_505448++;
        return 1;
    }
    g_ai_505448++;
    return 0;
}

/* the last time t was seen (+0xa94c): now while the user does not hide (0x409a20), else when this frame's
   line of sight (AI_CanSee 0x413570) finds it. 1 when that is more than 5 s ago */
static int lost_for_5s(Node *n, Node *t, int *hidden)
{
    uint8_t *ai = *Data_Ptr(n->data, 0x108);
    *hidden = g_ai_505458 != 0;
    if (!*hidden) {
        memcpy(ai + 0xa94c, &g_clock_game, 4);   /* 0x450e70 */
        return 0;
    }
    if (sight_turn() && AI_CanSee(n, t)) memcpy(ai + 0xa94c, &g_clock_game, 4);
    float seen;
    memcpy(&seen, ai + 0xa94c, 4);
    double s5 = fpu_add(seen, 5.0f);   /* 0x4a55dc */
    return s5 < g_clock_game || s5 != s5;
}

int AI_TargetLost(Node *n, Node *t)
{
    int hidden;
    return lost_for_5s(n, t, &hidden);
}

int AI_TargetSeen(Node *n, Node *t)
{
    int hidden, lost = lost_for_5s(n, t, &hidden);
    return hidden ? !lost : 1;
}

void AI_SearchTurns(void)
{
    g_ai_frames++;   /* 0x40fad0 */
    g_nav_asked = 0;
    if (!g_nav_busy && g_nav_owner) {   /* its search was dropped: the pool freed */
        Nav_Reset(*Data_Ptr(g_nav_owner->data, 0x108));
        g_nav_owner = NULL;
    }
    g_nav_busy = 0;
}
