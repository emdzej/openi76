/* The AI's road war (roadwar.h). In the current rounding mode (render/fpu.h). */
#include "roadwar.h"
#include "combat.h"
#include "drive.h"
#include "entity.h"
#include "exe.h"
#include "nav.h"
#include "platform.h"
#include "roads.h"
#include "render/fpu.h"
#include <math.h>
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
static void putf(uint8_t *p, float v) { memcpy(p, &v, 4); }
static int32_t bits(float f) { int32_t v; memcpy(&v, &f, 4); return v; }
static float fl(uint32_t b) { float f; memcpy(&f, &b, 4); return f; }
static uint8_t *ai_of(Node *n) { return *Data_Ptr(n->data, 0x108); }   /* 0x42d380 */
static bool is_vehicle(const Node *n) { return n->cls == 1 || n->cls == 8 || n->cls == 9; }   /* 0x42e570 */

/* the place in an AI block */
enum { P_INDEX = 0xa950, P_DIR = 0xa954, P_LANE = 0xa958, P_ASPECT = 0xa95c, P_OFF = 0xa964, P_HEAD = 0xa968,
       P_PIECE = 0xa96c };
static const RoadPiece **piece(uint8_t *ai) { return (const RoadPiece **)Data_Ptr(ai, P_PIECE); }
static const float *point(const RoadPiece *r, int32_t i) { return &r->pts[i][3]; }   /* the line's point */

/* Roads_Nearest into a place (the index left alone without roads) */
static void nearest(float x, float z, uint8_t *ai)
{
    int32_t i = rd32(ai + P_INDEX);
    Roads_Nearest(x, z, (RoadPiece **)Data_Ptr(ai, P_PIECE), &i);
    put32(ai + P_INDEX, i);
}

/* the way from a's place to b's along the strip: 1 when b's piece lies on from a's (or later on a's own),
   -1 otherwise (inlined in 0x41ba90 and 0x41c6b0) */
static int way_to(const RoadPiece *a, int32_t ia, const RoadPiece *b, int32_t ib)
{
    if (a == b) return ib > ia ? 1 : -1;
    const RoadPiece *p = a;
    if (!p->last)
        for (;;) {
            if (p == b) return 1;
            p = p->next;
            if (p->last) break;
        }
    return p == b ? 1 : -1;
}

/* 0x41b0d0: (x, y, z)'s distance from the line through a and b (*len) and the unit vector to it (*out), or
   the segment turned a quarter when it lies on the line */
static void line_offset(float x, float y, float z, const float a[3], const float b[3], float out[3], float *len)
{
    double ax = SUB(x, a[0]), ay = SUB(y, a[1]), az = SUB(z, a[2]);
    float dx = F32(SUB(b[0], a[0])), dy = F32(SUB(b[1], a[1])), dz = F32(SUB(b[2], a[2]));
    double dot = ADD(MUL(ax, dx), ADD(MUL(ay, dy), MUL(az, dz)));
    double l2 = ADD(MUL(dx, dx), ADD(MUL(dz, dz), MUL(dy, dy)));
    double t = DIV(dot, l2);
    double px = ADD(MUL(DIV(dx, l2), dot), a[0]);
    float ty = F32(MUL(t, dy)), tz = F32(MUL(t, dz));
    float rx = F32(SUB(x, px));
    double rzx = SUB(z, ADD(a[2], tz));
    float ry = F32(SUB(y, ADD(a[1], ty))), rz = F32(rzx);
    double l = fpu_sqrt(ADD(MUL(rx, rx), ADD(MUL(rzx, rz), MUL(ry, ry))));
    *len = F32(l);
    if (l > -9.999999747378752e-05 && bits(*len) < 0x38d1b717) {   /* 0x4a568c */
        out[0] = -dz, out[1] = dy, out[2] = dx;
        return;
    }
    double inv = DIV(1.0, *len);
    out[0] = F32(MUL(rx, inv)), out[1] = F32(MUL(ry, inv)), out[2] = F32(MUL(inv, rz));
}

void Road_Place(float x, float y, float z, uint8_t *ai)
{
    const RoadPiece *r = *piece(ai);
    int32_t i = rd32(ai + P_INDEX);
    const float *a = point(r, i);
    if (!Road_Next(&r, rd32(ai + P_DIR), &i)) {
        put32(ai + P_LANE, 2);
        put32(ai + P_OFF, 0x405ccccd);   /* 3.45 */
        return;
    }
    const float *b = point(r, i);
    float v[3], off;
    line_offset(x, y, z, a, b, v, &off);
    putf(ai + P_OFF, off);
    float ex = F32(SUB(b[0], a[0])), ey = F32(SUB(b[1], a[1]));
    double ez = SUB(b[2], a[2]);
    float s = F32(ADD(ADD(MUL(-ez, v[0]), MUL(v[1], ey)), MUL(v[2], ex)));
    bool left = rd32(ai + P_DIR) == 1 ? bits(s) <= 0 : (uint32_t)bits(s) <= 0x80000000u;
    int32_t ob = bits(off);
    if (left) {
        put32(ai + P_LANE, ob < 0x40a00000 ? 1 : 0);
        putf(ai + P_OFF, -off);
    } else
        put32(ai + P_LANE, ob < 0x40a00000 ? 2 : ob < 0x41200000 ? 3 : ob < 0x41700000 ? 4 : 5);   /* 5, 10, 15 */
}

int Road_Aspect(Node *n, uint8_t *ai, int band, Node *other)
{
    if (band < 1) {
        float h = AI_Heading(n);
        float a = Pair_Angle(n, other);
        double d = SUB(a, h);
        if (a < h) d = ADD(d, 6.2831854820251465f);   /* 0x4a567c */
        float df = F32(d);
        if (bits(df) > 0x40afeddf) return 0;   /* 7/4 pi */
        double q = MUL(ADD(df, 0.7853981852531433f), 0.6366197466850281f);   /* 0x4a5684, 0x4a5688 */
        return (int)floor(q);
    }
    const RoadPiece *r = *piece(ai);
    int32_t i = rd32(ai + P_INDEX);
    const float *a = point(r, i);
    if (!Road_Next(&r, rd32(ai + P_DIR), &i)) return 2;
    const float *b = point(r, i);
    float ex = F32(SUB(b[0], a[0])), ey = F32(SUB(b[1], a[1])), ez = F32(SUB(b[2], a[2]));
    float m0 = n->frame.m[6], m1 = n->frame.m[7], m2 = n->frame.m[8];
    double dot = ADD(ADD(MUL(ez, m2), MUL(ey, m1)), MUL(ex, m0));
    if (dot < -0.7070000171661377f) return 2;   /* 0x4a5670 */
    if (bits(F32(dot)) > 0x3f34fdf4) return 0;   /* 0.707 */
    double w = ADD(MUL(-ex, ADD(m0, m2)), MUL(m1, ey));
    return w > 0.0 ? 3 : 1;
}

int Road_Steer(Node *n, float x, float y, float z, uint8_t *ai, float lead, float offset, float *out)
{
    const RoadPiece *r0 = *piece(ai), *r = r0;
    int32_t i0 = rd32(ai + P_INDEX), i = i0, dir = rd32(ai + P_DIR);
    if (!Road_Next(&r, dir, &i)) {
        *out = F32(atan2(n->frame.m[6], n->frame.m[8]));
        return 0;
    }
    const float *q = point(r, i), *p = point(r0, i0);
    float k = F32(MUL((double)dir, offset));
    float dx = F32(SUB(q[0], p[0])), dy = F32(SUB(q[1], p[1])), dz = F32(SUB(q[2], p[2]));
    float len = F32(fpu_sqrt(ADD(ADD(MUL(dy, dy), MUL(dz, dz)), MUL(dx, dx))));
    float o[3] = {-dz, dy, dx};
    if ((uint32_t)bits(len) >= 0xb8d1b717u || bits(len) >= 0x38d1b717) {
        double f = DIV(k, len);
        o[0] = F32(MUL(o[0], f)), o[1] = F32(MUL(o[1], f)), o[2] = F32(MUL(f, o[2]));
    }
    float c[3] = {F32(ADD(o[0], p[0])), F32(ADD(o[1], p[1])), F32(ADD(o[2], p[2]))};
    float ux = F32(SUB(c[0], x)), uy = F32(SUB(c[1], y)), uz = F32(SUB(c[2], z));
    double dist = fpu_sqrt(ADD(ADD(MUL(uy, uy), MUL(uz, uz)), MUL(ux, ux)));
    float rl = F32(dist);
    if (dist > lead) {
        *out = F32(atan2(ux, uz));
        return 1;
    }
    r = r0, i = i0;   /* from the place again: the first step is q once more */
    if (!Road_Next(&r, dir, &i)) {
        *out = F32(atan2(ux, uz));
        return 0;
    }
    float rem = F32(SUB(lead, rl));
    for (;;) {
        float prev[3] = {c[0], c[1], c[2]};
        const float *pp = point(r, i);
        for (int k3 = 0; k3 < 3; k3++) c[k3] = F32(ADD(o[k3], pp[k3]));
        float ex = F32(SUB(c[0], prev[0])), ey = F32(SUB(c[1], prev[1])), ez = F32(SUB(c[2], prev[2]));
        double l = fpu_sqrt(ADD(ADD(MUL(ey, ey), MUL(ez, ez)), MUL(ex, ex)));
        rl = F32(l);
        if (!(l < rem)) {
            /* the rest along this step, but from its far end (a quirk: it overshoots) */
            double ax = ADD(MUL(DIV(ex, rl), rem), c[0]), az = ADD(MUL(DIV(ez, rl), rem), c[2]);
            *out = F32(atan2(SUB(ax, x), SUB(az, z)));
            return 1;
        }
        if (!Road_Next(&r, dir, &i)) {
            *out = F32(atan2(ex, ez));
            return 0;
        }
        rem = F32(SUB(rem, rl));
    }
}

/* the speed after steering (0x41ba90's cases): cruising over 0.75, 30 over 0.5, else 100 */
static void war_speed(Node *n)
{
    double a = fabs(rdf(n->data + 0xe0));
    if (a > 0.75) AI_Speed(n, AI_Cruise(n));   /* 0x4a5698 */
    else if (a > 0.5) AI_Speed(n, 30.0f);      /* 0x4a56a0 */
    else AI_Speed(n, 100.0f);
}

void Road_Pace(Node *n, float x, float y, float z, uint8_t *ai, uint8_t *tai, float lead, Node *t)
{
    float toff = rdf(tai + P_OFF), k;
    if ((uint32_t)bits(toff) > 0xc1400000u) k = -10.0f;   /* beyond -12 */
    else if (bits(toff) > 0x41b00000) k = 20.0f;          /* beyond 22 */
    else k = F32(ADD(toff, 4.0f));                        /* 0x4a56c0 */
    float h;
    Road_Steer(n, x, y, z, ai, lead, k, &h);
    AI_Steer(n, h);
    if (fabs(rdf(n->data + 0xe0)) > 0.6000000238418579) {   /* 0x4a56c8 */
        AI_Speed(n, AI_Cruise(n));
        return;
    }
    int32_t di = rd32(tai + P_INDEX) - rd32(ai + P_INDEX);
    float v = F32(MUL(fabs((double)di), 5.0f));   /* 0x4a56d0 */
    AI_Speed(n, F32(ADD(rdf(t->data + 0xac), v)));
}

void Road_War(Node *n, Node *t, int mode)
{
    float x = F32(n->frame.pos[0]), y = F32(n->frame.pos[1]), z = F32(n->frame.pos[2]);
    float tx = F32(t->frame.pos[0]), ty = F32(t->frame.pos[1]), tz = F32(t->frame.pos[2]);
    uint8_t *ai = ai_of(n), *tai = ai_of(t);
    nearest(x, z, ai);
    nearest(tx, tz, tai);
    const RoadPiece *np = *piece(ai), *tp = *piece(tai);
    if (!np || !tp) {   /* the original reads through the null piece */
        plat_log("OpenI76: road war without roads");
        return;
    }
    int d = way_to(np, rd32(ai + P_INDEX), tp, rd32(tai + P_INDEX));
    put32(ai + P_DIR, d);
    put32(tai + P_DIR, -d);
    int32_t di = rd32(ai + P_INDEX) - rd32(tai + P_INDEX);
    int far = di < 0 ? -di : di;
    int band = far == 0 ? 0 : (far >= 9) + 1;
    Road_Place(x, y, z, ai);
    Road_Place(tx, ty, tz, tai);
    put32(ai + P_ASPECT, Road_Aspect(n, ai, band, t));
    put32(tai + P_ASPECT, Road_Aspect(t, tai, band, n));
    float off = rdf(ai + P_OFF), toff = rdf(tai + P_OFF);
    /* 2 when t is more than 2 m further out than n, 0 more than 2 m further in, else 1 (0x4a56e4) */
    int side = SUB(toff, 2.0f) > off ? 2 : !(ADD(toff, 2.0f) >= off) ? 0 : 1;
    uint32_t at = (uint32_t)(((((mode * 3 + side) * 3 + band) * 4 + rd32(ai + P_ASPECT)) * 4) + rd32(tai + P_ASPECT));
    uint32_t act = exe_u32(0x4d25d0 + at * 4);
    float k, h;
    switch (act) {
    case 0: k = toff; break;                         /* on t's line */
    case 1: k = F32(ADD(toff, 5.0f)); break;         /* 5 m past it (0x4a56d0) */
    case 2:                                          /* the other way, on the line or 10 m off */
        k = bits(toff) >= 0x40a00000 ? 0.0f : 10.0f;
        put32(ai + P_DIR, -rd32(ai + P_DIR));
        break;
    case 3: AI_Ram(n, t); return;
    case 4:
    case 6: Road_Pace(n, x, y, z, ai, tai, 20.0f, t); return;
    case 5:   /* t near the line (0 to 10 m, or -0): 10 m off when within 5, else on it */
        if (band <= 2 && (uint32_t)bits(toff) <= 0x80000000u && bits(toff) <= 0x41200000)
            k = bits(toff) < 0x40a00000 ? 10.0f : 0.0f;
        else
            k = d == 1 ? 1.7000000476837158f : 8.199999809265137f;   /* 0x4a56e8, 0x4a56ec */
        break;
    default: plat_log(exe_str(0x4d2efc)); return;   /* "Unknown atomic behavior in road war" (fatal) */
    }
    Road_Steer(n, x, y, z, ai, 20.0f, k, &h);
    AI_Steer(n, h);
    war_speed(n);
}

int Road_Off(Node *t)
{
    if (!is_vehicle(t)) return 1;
    int32_t c = rd32(t->data + 0x45c);
    return !(c > 1 && c < 5);
}

int Road_Together(Node *n, Node *t)
{
    if (n->cls == 9) return 0;
    if (Road_Off(t)) return 0;
    float x = F32(n->frame.pos[0]), z = F32(n->frame.pos[2]);
    uint8_t *ai = ai_of(n), *tai = ai_of(t);
    nearest(x, z, ai);
    nearest(x, z, tai);   /* n's place again (the original's) */
    const RoadPiece *a = *piece(ai), *b = *piece(tai);
    return a && b && a->id == b->id;
}

void Road_Chase(Node *n, Node *t, float left)
{
    float x = F32(n->frame.pos[0]), y = F32(n->frame.pos[1]), z = F32(n->frame.pos[2]);
    uint8_t *ai = ai_of(n);
    nearest(x, z, ai);
    const RoadPiece *p = *piece(ai);
    if (!p) {   /* the original reads through the null piece */
        plat_log("OpenI76: road chase without roads");
        return;
    }
    int32_t i = rd32(ai + P_INDEX);
    float k = left;
    if (p->last && p->n - 5 < i) {   /* the strip's end: back */
        put32(ai + P_DIR, -1);
        k = fl(0x41033333);   /* 8.2 */
    } else if ((!p->prev || p->prev->last) && i < 5) {   /* its start: on */
        put32(ai + P_DIR, 1);
        k = fl(0x3fd9999a);   /* 1.7 */
    } else if (is_vehicle(t)) {
        float tx = F32(t->frame.pos[0]), ty = F32(t->frame.pos[1]), tz = F32(t->frame.pos[2]);
        uint8_t *tai = ai_of(t);
        nearest(tx, tz, tai);
        const RoadPiece *tp = *piece(tai);
        if (tp && tp->id == p->id) {
            k = rdf(ai + P_OFF);
            int d = way_to(p, i, tp, rd32(tai + P_INDEX));
            put32(ai + P_DIR, d);
            Road_Place(x, y, z, ai);
            put32(tai + P_DIR, -d);
            Road_Place(tx, ty, tz, tai);
            put32(ai + P_DIR, -rd32(ai + P_DIR));
            int32_t tb = bits(rdf(tai + P_OFF));
            if (tb < 0x40600000) k = fl(0x41033333);        /* t within 3.5 m: 8.2 */
            else if (tb > 0x40d00000) k = fl(0x3fd9999a);   /* beyond 6.5: 1.7 */
        }
    }
    int32_t dir = rd32(ai + P_DIR);
    if (dir != 1 && dir != -1) {
        put32(ai + P_DIR, 1);
        k = fl(0x3fd9999a);
    }
    putf(ai + P_OFF, k);
    float h;
    if (Road_Steer(n, x, y, z, ai, 9.0f, k, &h)) {
        putf(ai + P_HEAD, h);
        AI_Steer(n, h);
        if (Pair_Dist(n, t) <= 50.0f && fabs(rdf(n->data + 0xe0)) <= 0.6499999761581421)   /* 0x4a5714, 0x4a5718 */
            AI_Speed(n, 50.0f);
        else
            AI_Speed(n, AI_Cruise(n));
    } else
        putf(ai + P_HEAD, h);
}
