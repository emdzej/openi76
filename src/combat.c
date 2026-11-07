/* The AI's fighting (combat.h). In the current rounding mode (render/fpu.h). */
#include "combat.h"
#include "drive.h"
#include "entity.h"
#include "exe.h"
#include "math3d.h"
#include "orders.h"
#include "platform.h"
#include "weapons.h"
#include "render/fpu.h"
#include <math.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#define ADD fpu_add
#define SUB fpu_sub
#define MUL fpu_mul
#define DIV fpu_div
#define F32 fpu_f32

static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }
static void put32(uint8_t *p, int32_t v) { memcpy(p, &v, 4); }
static float rdf(const uint8_t *p) { float v; memcpy(&v, p, 4); return v; }
static int32_t bits(float f) { int32_t v; memcpy(&v, &f, 4); return v; }
static uint32_t ubits(float f) { return (uint32_t)bits(f); }
static uint8_t *ai_of(Node *n) { return *Data_Ptr(n->data, 0x108); }   /* 0x42d380 */
static bool is_vehicle(const Node *n) { return n->cls == 1 || n->cls == 8 || n->cls == 9; }   /* 0x42e570 */

void Node_CentreNow(float out[4], Node *n)
{
    Node *top = n;
    while (top->prev) top = top->prev;   /* 0x42e6b0 */
    Entity *e = NULL;
    for (int i = 0; i < MAX_ENTITIES && !e; i++)
        if (g_entities[i].node == top) e = &g_entities[i];
    if (!e) {
        memset(out, 0, 4 * sizeof out[0]);
        return;
    }
    out[3] = e->r;
    Frame f;
    if (!(e->node->flags & 0x100)) f = e->node->frame;
    else Node_FrameIn(&f, n, NULL);
    float p[3];
    Frame_Points((float(*)[3])p, (const float(*)[3])e->c, 1, &f);
    memcpy(out, p, sizeof p);
}

/* The relations' cache (0x421fc0-0x423b50): what a node's heading and centre (0x50cd80, 200 entries of
   0x20) and a pair's distances and headings (0x50e848, 0x34 each) were when first asked for, until the
   frame's end (Rel_Clear). A pair's entry is made from its nodes' entries, the one at the lower address
   first: its distances from that one's centre less the other's, rounded so, its headings each from the
   difference taken its own way. The port orders the nodes by when they were made (Node.serial), as the
   heap hands them out. The original's hash chains only find entries; a list does the same. */
typedef struct { Node *n; float h, c[4]; } RelNode;
typedef struct { Node *lo, *hi; float d2, d3, a_lh, a_hl; } RelPair;
#define REL_NODES 200
#define REL_PAIRS 2048
static RelNode rel_node[REL_NODES];
static RelPair rel_pair[REL_PAIRS];
static int rel_nodes, rel_pairs;

void Rel_Clear(void) { rel_nodes = rel_pairs = 0; }

/* 0x422c20 and its copies: the node's entry, made when missing */
static const RelNode *rel_node_of(Node *n)
{
    for (int i = 0; i < rel_nodes; i++)
        if (rel_node[i].n == n) return &rel_node[i];
    static RelNode spare;
    RelNode *e = rel_nodes < REL_NODES ? &rel_node[rel_nodes++] : &spare;   /* the original runs off its table */
    if (e == &spare) plat_log("OpenI76: the relations' node table is full");
    e->n = n;
    e->h = AI_HeadingNow(n);
    Node_CentreNow(e->c, n);
    return e;
}

/* 0x4227b0 and 0x423220's entry, made when missing */
static const RelPair *rel_pair_of(Node *a, Node *b)
{
    Node *lo = a, *hi = b;
    if (hi->serial < lo->serial) lo = b, hi = a;
    for (int i = 0; i < rel_pairs; i++)
        if (rel_pair[i].lo == lo && rel_pair[i].hi == hi) return &rel_pair[i];
    const RelNode *l = rel_node_of(lo), *h = rel_node_of(hi);
    static RelPair spare;
    RelPair *e = rel_pairs < REL_PAIRS ? &rel_pair[rel_pairs++] : &spare;
    if (e == &spare) plat_log("OpenI76: the relations' pair table is full");
    e->lo = lo, e->hi = hi;
    float dx = F32(SUB(l->c[0], h->c[0])), dy = F32(SUB(l->c[1], h->c[1])), dz = F32(SUB(l->c[2], h->c[2]));
    e->a_hl = fabsf(dx) == 0 && fabsf(dz) == 0 ? 0 : F32(atan2(dx, dz));   /* fpatan */
    double xx = MUL(dx, dx), zz = MUL(dz, dz), yy = MUL(dy, dy);
    e->d2 = F32(fpu_sqrt(ADD(zz, xx)));
    e->d3 = F32(fpu_sqrt(ADD(xx, ADD(zz, yy))));
    float ex = F32(SUB(h->c[0], l->c[0])), ez = F32(SUB(h->c[2], l->c[2]));
    e->a_lh = fabsf(ex) == 0 && fabsf(ez) == 0 ? 0 : F32(atan2(ex, ez));
    return e;
}

float AI_Heading(Node *n) { return rel_node_of(n)->h; }

void Node_Centre(float out[4], Node *n) { memcpy(out, rel_node_of(n)->c, 4 * sizeof out[0]); }

float Pair_Angle(Node *a, Node *b)
{
    const RelPair *e = rel_pair_of(a, b);
    return e->lo == a ? e->a_lh : e->a_hl;
}

float Pair_Dist(Node *a, Node *b) { return rel_pair_of(a, b)->d2; }

float Pair_Dist3(Node *a, Node *b) { return rel_pair_of(a, b)->d3; }

int Angle_Sector(float a, float b)
{
    double dx = SUB(b, a);
    if (b < a) dx = ADD(dx, 6.2831854820251465f);   /* 0x4a5a74 */
    float d = F32(dx);
    if (bits(d) > 0x40bc7edd) d = F32(SUB(d, 5.890486240386963f));   /* 0x4a5a78 */
    else d = F32(ADD(d, 0.39269909262657166f));                    /* 0x4a5a7c */
    return (int32_t)(int64_t)floor(MUL(d, 1.2732394933700562f));   /* 0x4a5a80 */
}

void Class_Velocity(Node *n, float v[3], float *speed)
{
    switch (n->cls) {
    case 1: case 9:   /* 0x42a740, 0x431d70 */
        memcpy(v, n->data + 0xbc, 12);
        *speed = rdf(n->data + 0xac);
        return;
    case 7: case 8: case 0xa: {   /* 0x433000, 0x428c70, 0x4347a0 */
        static int told;
        if (!told++) plat_log("OpenI76: the velocity of classes 7, 8 and 0xa (0x425be0) is not ported yet");
        break;
    }
    }
    v[0] = v[1] = v[2] = 0;
    *speed = 0;
}

/* AI_Closest 0x4139f0 (combat.h): when p1 moving at v1 and p2 at v2 come closest; 1e7 for nearly equal velocities */
double AI_Closest(const float p1[3], const float v1[3], const float p2[3], const float v2[3])
{
    float dx = F32(SUB(p1[0], p2[0])), dy = F32(SUB(p1[1], p2[1])), dz = F32(SUB(p1[2], p2[2]));
    float wx = F32(SUB(v2[0], v1[0])), wy = F32(SUB(v2[1], v1[1]));
    double wzx = SUB(v2[2], v1[2]);
    float wz = F32(wzx);
    double l2x = ADD(ADD(MUL(wzx, wz), MUL(wy, wy)), MUL(wx, wx));
    float l2 = F32(l2x);
    if (fabs(l2x) < 0.01f) return 1e7f;   /* 0x4a53e4, 0x4a53e8 */
    return DIV(ADD(ADD(MUL(wy, dy), MUL(wz, dz)), MUL(wx, dx)), l2);
}

/* 0x413810: where t will be in time s (+0.09): along its velocity, or round its turn (+0xcc the yaw rate) */
static void predict(Node *t, float s, float out[3])
{
    if (!is_vehicle(t)) {
        float c[4];
        Node_Centre(c, t);
        memcpy(out, c, 12);
        return;
    }
    uint8_t *d = t->data;
    float w = rdf(d + 0xcc);
    Frame f = t->frame;
    float p[3] = {F32(f.pos[0]), F32(f.pos[1]), F32(f.pos[2])};
    float v[3], sp;
    Class_Velocity(t, v, &sp);
    double aw = fabs(w);
    if (aw < 0.05) {   /* 0x4a53d0 */
        double T = ADD(s, 0.09f);   /* 0x4a53d8 */
        out[0] = F32(ADD(MUL(v[0], T), p[0]));
        out[1] = F32(ADD(MUL(v[1], T), p[1]));
        out[2] = F32(ADD(MUL(T, v[2]), p[2]));
        return;
    }
    float sign = bits(w) > 0 ? -1.0f : 1.0f;
    double A = MUL(rdf(d + 0xe8), sign), R = DIV(sp, aw);
    double ang = MUL(SUB(MUL(ADD(s, 0.09f), w), 1.5707963705062866f), A);   /* 0x4a53e0 */
    float local[3] = {F32(MUL(sin(ang), R)), 0, F32(MUL(cos(ang), R))};
    f.pos[0] = ADD(MUL(MUL(f.m[0], A), R), p[0]);
    f.pos[1] = p[1];
    f.pos[2] = ADD(MUL(R, MUL(A, f.m[2])), p[2]);
    Frame_Points((float(*)[3])out, (const float(*)[3])local, 1, &f);
}

/* the time to t's closest approach, within [0.01, 5] */
static float approach(Node *n, Node *t, const float me[3], const float v[3], const float tp[3])
{
    float tv[3], tsp;
    Class_Velocity(t, tv, &tsp);
    double x = AI_Closest(me, v, tp, tv);
    float s = F32(x);
    if (x < 0.01f) s = 0.01f;   /* 0x4a5770 */
    else if (bits(s) > 0x40a00000) s = 5.0f;
    (void)n;
    return s;
}

/* 0x41d900: chasing t, aiming where it will be when its shots (the first forward weapon's speed) or the
   vehicle meet it; the speed by the distance (less 20 m) and the target's own */
static void pursue(Node *n, Node *t)
{
    float me[3] = {F32(n->frame.pos[0]), F32(n->frame.pos[1]), F32(n->frame.pos[2])};
    float tc[4];
    Node_Centre(tc, t);
    float d[3], tsp = 0, s = 0;
    if (!is_vehicle(t)) {
        for (int k = 0; k < 3; k++) d[k] = F32(SUB(tc[k], me[k]));
    } else {
        float v[3], sp;
        Class_Velocity(n, v, &sp);
        uint8_t *ai = ai_of(n);
        if (rd32(ai + 0xa6f4)) {
            float k = rdf(ai + 0xa74c + rd32(ai + 0xa6f8) * 0x20);
            const float *f = &n->frame.m[6];
            float vx = F32(ADD(MUL(f[0], k), v[0]));
            float fy = F32(MUL(f[1], k)), fz = F32(MUL(k, f[2]));
            v[1] = F32(ADD(v[1], fy));
            v[2] = F32(ADD(v[2], fz));
            v[0] = vx;
        }
        float tv[3];
        Class_Velocity(t, tv, &tsp);
        double x = AI_Closest(me, v, tc, tv);
        s = F32(x);
        if (x < 0.01f) s = 0.01f;
        else if (bits(s) > 0x40a00000) s = 5.0f;
        float p[3];
        predict(t, s, p);
        for (int k = 0; k < 3; k++) d[k] = F32(SUB(p[k], me[k]));
    }
    float a = F32(atan2(d[0], d[2]));
    double l = SUB(fpu_sqrt(ADD(ADD(MUL(d[1], d[1]), MUL(d[2], d[2])), MUL(d[0], d[0]))), 20.0);   /* 0x4a5778 */
    float dist = 0 < l ? F32(l) : 0.0f;
    AI_Steer(n, a);
    bool hard = !(fabs(rdf(n->data + 0xe0)) < 0.4000000059604645);   /* 0x4a5790 */
    if (bits(tsp) < 0x3f800000) {   /* a slow target */
        if (hard) AI_Speed(n, AI_Cruise(n));
        else if (bits(dist) < 0x40800000) AI_Stop(n);
        else {
            double v = MUL(SUB(dist, 1.0f), 0.3f);   /* 0x4a5788, 0x4a579c */
            AI_Speed(n, 0 < v ? F32(v) : 0.0f);
        }
    } else if (!hard) {
        if (bits(dist) > 0x43160000) AI_Speed(n, 1000.0f);
        else if (bits(dist) < 0x3f800000) AI_Speed(n, F32(MUL(tsp, 0.5f)));   /* 0x4a57ac */
        else if (bits(dist) > 0x42480000) {
            double v = MUL(SUB(dist, 2.0f), 0.7f);   /* 0x4a57b8, 0x4a57bc */
            AI_Speed(n, F32(ADD(0 < v ? v : 0.0, tsp)));
        } else {
            double v = SUB(dist, 1.0f);
            AI_Speed(n, F32(ADD(MUL(0 < v ? v : 0.0, 0.4f), tsp)));   /* 0x4a57c0 */
        }
    } else if (bits(dist) >= 0x42c80000)
        AI_Speed(n, AI_Cruise(n));
    else {
        const float *f = &n->frame.m[6], *g = &t->frame.m[6];
        float dot = F32(ADD(ADD(MUL(f[1], d[1]), MUL(f[2], d[2])), MUL(f[0], d[0])));
        double same = ADD(ADD(MUL(g[0], f[0]), MUL(g[1], f[1])), MUL(g[2], f[2]));
        if (same > 0.7f && ubits(dot) > 0x80000000u) AI_Speed(n, F32(MUL(tsp, 0.75f)));   /* 0x4a57c8 */
        else if (bits(s) > 0) AI_Speed(n, F32(SUB(tsp, 5.0f)));   /* 0x4a5774 */
        else AI_Speed(n, AI_Cruise(n));
    }
}

/* 0x41ddf0: ramming, flat out at where t will be */
void AI_Ram(Node *n, Node *t)
{
    float me[3] = {F32(n->frame.pos[0]), F32(n->frame.pos[1]), F32(n->frame.pos[2])};
    float tc[4];
    Node_Centre(tc, t);
    double dx, dz;
    if (!is_vehicle(t)) {
        dx = SUB(tc[0], me[0]);
        dz = SUB(tc[2], me[2]);
    } else {
        float v[3], sp, p[3];
        Class_Velocity(n, v, &sp);
        predict(t, approach(n, t, me, v, tc), p);
        dx = SUB(p[0], me[0]);
        dz = SUB(p[2], me[2]);
    }
    float a = F32(atan2(dx, dz));
    AI_Speed(n, 1000.0f);
    AI_Steer(n, a);
}

/* 0x414db0: the velocity that meets t (its place in a time by the distance over the speed + 25, within
   [0.4, 1.75]), sped to the vehicle's speed x 0.9, or 1000 x when still; kept when it is not backward */
static void meet(float out[3], Node *n, Node *t)
{
    double ax = n->frame.pos[0], ay = n->frame.pos[1], az = n->frame.pos[2];
    float fx = F32(SUB(F32(t->frame.pos[0]), F32(ax))), fy = F32(SUB(F32(t->frame.pos[1]), F32(ay)));
    float fz = F32(SUB(F32(t->frame.pos[2]), F32(az)));
    float v[3], sp;
    Class_Velocity(n, v, &sp);
    float s = F32(DIV(fpu_sqrt(ADD(ADD(MUL(fx, fx), MUL(fy, fy)), MUL(fz, fz))), ADD(sp, 25.0f)));   /* 0x4a54b4 */
    if (s < 0.4f) s = 0.4f;   /* 0x4a5408 */
    else if (bits(s) > 0x3fe00000) s = 1.75f;
    float p[3];
    predict(t, s, p);
    float wx = F32(DIV(SUB(p[0], F32(ax)), s)), wy = F32(DIV(SUB(p[1], F32(ay)), s)), wz = F32(DIV(SUB(p[2], F32(az)), s));
    float w = F32(fpu_sqrt(ADD(ADD(MUL(wx, wx), MUL(wy, wy)), MUL(wz, wz))));
    float r[3];
    if (w <= 25.0f) {
        if (ubits(w) < 0xb8d1b717u && bits(w) < 0x38d1b717) {
            r[0] = F32(MUL(wx, 1000.0f)), r[1] = F32(MUL(wy, 1000.0f)), r[2] = F32(MUL(wz, 1000.0f));   /* 0x4a5410 */
        } else {
            float k = F32(MUL(DIV(sp, w), 0.9f));   /* 0x4a54c0 */
            r[0] = F32(MUL(wx, k)), r[1] = F32(MUL(wy, k)), r[2] = F32(MUL(k, wz));
        }
    } else {
        float k = F32(ADD(DIV(-25.0f, w), 1.0f));   /* 0x4a54bc, 0x4a53dc */
        r[0] = F32(MUL(wx, k)), r[1] = F32(MUL(wy, k)), r[2] = F32(MUL(k, wz));
    }
    if (0.0f < ADD(ADD(MUL(v[0], r[0]), MUL(v[1], r[1])), MUL(v[2], r[2]))) memcpy(out, r, sizeof r);   /* 0x4a53bc */
    else out[0] = wx, out[1] = wy, out[2] = wz;
}

/* 0x41df40: matching t's course (meet's velocity), braking near a still one */
static void match(Node *n, Node *t, float heading, float dist)
{
    uint8_t *d = n->data;
    float m[3];
    meet(m, n, t);
    float a = heading;
    if (!(fabsf(m[0]) < 9.999999747378752e-05) || !(fabsf(m[2]) < 9.999999747378752e-05)) a = F32(atan2(m[0], m[2]));   /* 0x4a57d0 */
    AI_Steer(n, a);
    float tv[3], tsp;
    Class_Velocity(t, tv, &tsp);
    if (bits(tsp) > 0x3f7fffff) {
        if (fabsf(rdf(d + 0xe4)) > 0.4000000059604645) AI_Speed(n, AI_Cruise(n));
        else AI_Speed(n, F32(fpu_sqrt(ADD(ADD(MUL(m[0], m[0]), MUL(m[2], m[2])), MUL(m[1], m[1])))));
        return;
    }
    if (fabs(Angle_Diff(heading, a)) >= 0.19f) AI_Speed(n, AI_Cruise(n));   /* 0x4a57d8 */
    else if (bits(dist) < 0x42700000) AI_Stop(n);
    else AI_Speed(n, F32(MUL(SUB(dist, 60.0f), 0.2f)));   /* 0x4a57dc, 0x4a57e0 */
}

void Combat(Node *n, Node *t, int mode)
{
    float h = AI_Heading(n), a = Pair_Angle(n, t);
    int mine = Angle_Sector(h, a), theirs;
    float th = 0, ta = 0;
    if (!is_vehicle(t)) theirs = 4;
    else {
        th = AI_Heading(t);
        ta = Pair_Angle(t, n);
        theirs = (rd32(t->data + 0x454) & 2) ? 4 : Angle_Sector(th, ta);
    }
    double distx = Pair_Dist(n, t);
    float dist = F32(distx);
    int band;
    if (distx < 24.0f) band = bits(dist) < 0x41100000 ? 0 : 1;   /* 0x4a57f0 */
    else if (bits(dist) >= 0x43480000) band = 4;
    else band = bits(dist) < 0x42700000 ? 2 : 3;
    int8_t state;
    memcpy(&state, exe_data(0x4d2f20 + (uint32_t)((mine + (mode * 5 + band) * 8) * 8 + theirs), 1), 1);
    const float q = 0.7853981852531433f;   /* 0x4a57cc */
    switch (state) {
    case 0: AI_Speed(n, AI_Cruise(n)); AI_Steer(n, F32(SUB(h, q))); return;
    case 1: AI_Speed(n, 1000.0f); AI_Steer(n, F32(SUB(h, q))); return;
    case 2: AI_Speed(n, AI_Cruise(n)); AI_Steer(n, F32(ADD(h, q))); return;
    case 3: AI_Speed(n, 1000.0f); AI_Steer(n, F32(ADD(h, q))); return;
    case 4: pursue(n, t); return;
    case 5: AI_Ram(n, t); return;
    case 6: match(n, t, h, dist); return;
    case 7: {   /* side by side: away from its side */
        double x = Angle_Diff(h, a);
        bool left;   /* the same either way when nearly abreast (the float's sign word tested) */
        if (fabs(x) >= 1.5707963267948966) left = Angle_Diff(th, ta) < 0.0f;   /* 0x4a57e8, 0x4a57a0 */
        else left = bits(F32(Angle_Diff(th, ta))) > 0;
        AI_Speed(n, 1000.0f);
        AI_Steer(n, left ? F32(SUB(h, q)) : F32(ADD(h, q)));
        return;
    }
    case 8: {
        double x = Angle_Diff(h, a);
        pursue(n, t);
        if (bits(dist) < 0x41c00000 && F32(fabs(x)) < (float)0.19) AI_Stop(n);   /* 0x4a57f8 */
        return;
    }
    case 10: AI_Speed(n, 1000.0f); AI_Steer(n, h); return;
    case 11: AI_Speed(n, AI_Cruise(n)); AI_Steer(n, h); return;
    case 12: case 13: return;
    case 14: {
        double x = Angle_Diff(h, a);
        pursue(n, t);
        if (bits(dist) < 0x430c0000 && F32(fabs(x)) < (float)0.1) AI_Stop(n);   /* 0x4a5808 */
        return;
    }
    default:
        plat_log(exe_str(0x4d36a0));   /* 0x402c80, the reporter */
    }
}

/* 0x455080: a weapon instance's facts for the AI (0x20 bytes); 0 for a freed instance */
static int weapon_facts(int32_t inst, int32_t fact[8])
{
    const WeaponInst *w = &g_weapons[inst];
    if (w->index != inst) return 0;
    const WeaponDef *def = &g_weapon_defs[w->def];
    fact[0] = def->index;
    fact[1] = def->kind;
    fact[7] = w->hp->slot[w->slot].type;
    fact[2] = def->v[0];
    fact[3] = def->v[2];
    fact[4] = def->v[9];
    int o = 0;   /* 0x45c920 over all 20 kinds */
    while (o < 20 && g_ordnance[o].type != def->v[8]) o++;
    if (o < 20) fact[5] = g_ordnance[o].w13;   /* 0x458f10: the kind's speed */
    else {
        char m[96];
        snprintf(m, sizeof m, "OpenI76: weapon %d's ordnance %d is missing (0x458f10 reads before the table)", inst,
                 def->v[8]);
        plat_log(m);
        fact[5] = 0;
    }
    fact[6] = w->hp->slot[w->slot].id;
    return 1;
}

void Weapons_Refresh(Node *n)
{
    uint8_t *ai = ai_of(n);
    if (!rd32(ai + 0xa714)) return;
    int h = 0;   /* 0x454f80 */
    while (h < g_hardpoint_count && g_hardpoints[h].vehicle != n) h++;
    if (h == g_hardpoint_count)
        put32(ai + 0xa718, 0);
    else {
        const HardPoints *hp = &g_hardpoints[h];
        int32_t list[8] = {0}, k = 0;
        for (int s = 0; s < hp->count; s++)
            if (hp->slot[s].inst != -1) list[1 + k++] = hp->slot[s].inst;
        list[6] = hp->slot[5].inst;
        list[7] = hp->slot[6].inst;
        list[0] = k;
        memcpy(ai + 0xa718, list, sizeof list);
        for (int i = 0; i < rd32(ai + 0xa718); i++) {
            int32_t fact[8];
            memcpy(fact, ai + 0xa738 + i * 0x20, sizeof fact);
            if (!weapon_facts(rd32(ai + 0xa71c + i * 4), fact)) {
                char m[64];
                snprintf(m, sizeof m, exe_str(0x4a8938), i);
                plat_log(m);
            }
            memcpy(ai + 0xa738 + i * 0x20, fact, sizeof fact);
        }
    }
    /* 0x4089d0: those whose muzzle (0x4551d0's frame) faces within 0.96 of the vehicle's z axis; a
       type 3 slot's (0x456810) is told its place in the list instead, unless it is the user's */
    const float *f = &n->frame.m[6];
    put32(ai + 0xa6f4, 0);
    for (int i = 0; i < rd32(ai + 0xa718); i++) {
        int32_t inst = rd32(ai + 0xa71c + i * 4);
        if (rd32(ai + 0xa754 + i * 0x20) == 3) {
            if (!g_user_entity || g_user_entity->node != n) g_weapons[inst].hp->w1[0] = i;
            continue;
        }
        if (g_weapons[inst].index != inst) plat_log("OpenI76: a freed weapon's frame (0x4551d0 leaves it unset)");
        Frame m;
        Node_FrameIn(&m, g_weapons[inst].muzzle, NULL);
        if (ADD(ADD(MUL(m.m[7], f[1]), MUL(m.m[8], f[2])), MUL(m.m[6], f[0])) > 0.96f) {   /* 0x4a5170 */
            int32_t c = rd32(ai + 0xa6f4);
            put32(ai + 0xa6f8 + c * 4, i);
            put32(ai + 0xa6f4, c + 1);
        }
    }
    put32(ai + 0xa714, 0);
}
