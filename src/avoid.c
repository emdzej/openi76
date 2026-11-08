/* The AI's look ahead (avoid.h). In the current rounding mode (render/fpu.h). */
#include "avoid.h"
#include "clock.h"
#include "collide.h"
#include "combat.h"
#include "crt.h"
#include "drive.h"
#include "entity.h"
#include "exe.h"
#include "orders.h"
#include "physics.h"
#include "platform.h"
#include "terrain.h"
#include "weapons.h"
#include "render/fpu.h"
#include <math.h>
#include <string.h>

#define ADD fpu_add
#define SUB fpu_sub
#define MUL fpu_mul
#define DIV fpu_div
#define F32 fpu_f32

static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }
static float rdf(const uint8_t *p) { float v; memcpy(&v, p, 4); return v; }
static int32_t bits(float f) { int32_t v; memcpy(&v, &f, 4); return v; }
static uint32_t ubits(float f) { return (uint32_t)bits(f); }
static float from(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static uint8_t *ai_of(Node *n) { return *Data_Ptr(n->data, 0x108); }   /* 0x42d380 */
static bool is_vehicle(const Node *n) { return n->cls == 1 || n->cls == 8 || n->cls == 9; }   /* 0x42e570 */
/* the original's "tiny" test of a float: within 1e-4 of 0 by its bits */
static bool tiny(float f) { return ubits(f) < 0xb8d1b717u && bits(f) < 0x38d1b717; }
/* x87 comparisons: "below" also takes unordered */
static bool below(double a, double b) { return !(a >= b); }
static bool below_eq(double a, double b) { return !(a > b); }

/* ---- the collision lists, moving first (0x46b1e0, 0x46b230) ---- */

typedef struct {
    Cell *c;
    bool moving;
} CollIter;

static Entity *coll_first(CollIter *it)
{
    it->moving = g_coll_moving != NULL;
    it->c = it->moving ? g_coll_moving : g_coll_static;
    return it->c ? it->c->value : NULL;
}

static Entity *coll_next(CollIter *it)
{
    if (!it->c) return NULL;
    it->c = it->c->next;
    if (!it->c && it->moving) {
        it->moving = false;
        it->c = g_coll_static;
    }
    return it->c ? it->c->value : NULL;
}

/* ---- the terrain ahead (0x415080) ---- */

/* the time t to the obstacle at distance d: d / speed, the most a float holds for a still vehicle */
static float time_to(float d, float speed) { return tiny(speed) ? from(0x7f7fffff) : F32(DIV(d, speed)); }

/* 0x415080: from p along f's z axis (times dir), every 2.5 m: forward (rough, dir over 0) from 2 m up to
   7.5 m, a step down of more than 3 m or up of more than 3.4 (0x4059999a), or the terrain's flag, stops
   it; else (sides) two lines 2.5 m to either side of the axis from 2.5 m to the larger of 20 and 1.5 x the
   speed: a side's step down of more than 1 or up of more than 1.5, a fall across of more than 1.5 (down
   from left to right) or a rise of 1.5, or the flag. *slow 0.5 (the speed's factor, min(d, 0.5)) and *t
   the time to it */
static int terrain_ahead(const Frame *f, const float p[3], float speed, float *slow, float dir, int rough,
                         float *t)
{
    float zx = F32(MUL(dir, f->m[6])), zz = F32(MUL(dir, f->m[8]));
    if (rough) {
        float h0 = F32(Terrain_Height(p[0], p[2]));
        float oz = F32(MUL(zz, 2.0f)), sz = F32(MUL(zz, 2.5f)), ox = F32(MUL(zx, 2.0f));   /* 0x4a54a4, 0x4a54c4 */
        float sx = F32(MUL(zx, 2.5f)), d = 2.0f;
        for (;;) {
            float px = F32(ADD(ox, p[0])), pz = F32(ADD(oz, p[2]));
            if (bits(dir) > 0 && Terrain_Flag(px, pz)) {
                *t = 0;
                return 1;
            }
            double hx = Terrain_Height(px, pz);
            double dh = SUB(hx, h0);
            if (below(dh, -3.0f) || bits(F32(dh)) > 0x4059999a) break;   /* 0x4a54c8 */
            oz = F32(ADD(oz, sz));
            ox = F32(ADD(ox, sx));
            double nd = ADD(d, 2.5f);
            h0 = F32(hx);
            d = F32(nd);
            if (!below_eq(nd, 7.5f)) return 0;   /* 0x4a54d0 */
        }
        *slow = 0.5f <= d ? 0.5f : d;   /* 0x4a54d4 */
        return 1;
    }
    float ax = F32(ADD(MUL(f->m[0], 2.5f), p[0])), az = F32(ADD(MUL(f->m[2], 2.5f), p[2]));
    float cx = F32(ADD(MUL(f->m[0], -2.5f), p[0]));   /* 0x4a54d8 */
    double czx = ADD(MUL(f->m[2], -2.5f), p[2]);
    float cz = F32(czx);
    float hc = F32(Terrain_Height(cx, czx)), ha = F32(Terrain_Height(ax, az));
    double lim = MUL(speed, 1.5f);   /* 0x4a53a8 */
    float limit = 20.0f < lim ? F32(lim) : 20.0f;   /* 0x4a5494 */
    float d = 2.5f;
    if (bits(limit) < 0x40200000) return 0;
    double kz = MUL(zz, 2.5f), kx = MUL(zx, 2.5f);
    float stz = F32(kz), stx = F32(kx);
    float bz = F32(ADD(az, kz)), dz = F32(ADD(kz, cz));
    float bx = F32(ADD(ax, kx)), dx = F32(ADD(kx, cx));
    for (;;) {
        int stop = 0;
        if (Terrain_Flag(dx, dz)) {
            *t = time_to(d, speed);
            return 1;
        }
        double h = Terrain_Height(dx, dz), dh = SUB(h, hc);
        if (below(dh, -1.0f) || bits(F32(dh)) > 0x3fc00000) stop = 1;   /* 0x4a53cc */
        else {
            hc = F32(h);
            double h2 = Terrain_Height(bx, bz), dh2 = SUB(h2, ha);
            float hb = F32(h2);
            if (below(dh2, -1.0f) || bits(F32(dh2)) > 0x3fc00000) stop = 1;
            else {
                double across = SUB(hc, hb);
                ha = hb;
                if (below(across, -1.5f) || bits(F32(across)) > 0x3fc00000) stop = 1;   /* 0x4a54dc */
            }
        }
        if (stop) {
            *t = time_to(d, speed);
            *slow = 0.5f <= d ? 0.5f : d;
            return 1;
        }
        bz = F32(ADD(bz, stz));
        bx = F32(ADD(bx, stx));
        dz = F32(ADD(dz, stz));
        dx = F32(ADD(dx, stx));
        double nd = ADD(d, 2.5f);
        d = F32(nd);
        if (!below_eq(nd, limit)) return 0;
    }
}

/* ---- the vehicles ahead (0x415da0) ---- */

/* 0x415b80: point k of ten about c: 2.5 m steps along z (2 to -2) and 1.3 m to either side along x */
static void probe_point(int k, const float c[3], const float x[3], const float z[3], float out[3])
{
    static const float along[10] = {2, 2, 1, 1, 0, 0, -1, -1, -2, -2};
    float a = along[k], b = (k & 1) ? -1.0f : 1.0f;
    float s[3];
    for (int i = 0; i < 3; i++) s[i] = F32(MUL(MUL(z[i], a), 2.5f));
    for (int i = 0; i < 3; i++) s[i] = F32(ADD(MUL(MUL(x[i], b), 1.3f), s[i]));   /* 0x4a54f8 */
    for (int i = 0; i < 3; i++) out[i] = F32(ADD(s[i], c[i]));
}

/* 0x4139f0's sum inlined: when p (moving v) and q (moving w) come closest; 1e7 under 0.01 */
static float closest(const float p[3], const float v[3], const float q[3], const float w[3])
{
    float r[3], u[3];
    for (int i = 0; i < 3; i++) r[i] = F32(SUB(p[i], q[i]));
    u[0] = F32(SUB(w[0], v[0]));
    u[1] = F32(SUB(w[1], v[1]));
    double u2x = SUB(w[2], v[2]);
    u[2] = F32(u2x);
    double l = ADD(ADD(MUL(u2x, u[2]), MUL(u[1], u[1])), MUL(u[0], u[0]));
    float lf = F32(l);
    if (below(fabs(l), 0.01f)) return 1e7f;   /* 0x4a53e4 */
    return F32(DIV(ADD(ADD(MUL(u[1], r[1]), MUL(u[2], r[2])), MUL(u[0], r[0])), lf));
}

/* 0x415da0: the other vehicles (class 1, and with all 8 too) whose look-ahead box meets this one's, within
   100 m: unless all, only those ahead within 60 degrees of the velocity. At the time of the closest
   approach (0-3 s) ten points of the other's box, seen from this one moved as far, within 2.5 m ahead
   and 1.3 to the side: *slow 1, *t that time, *factor min(t, 0.5) (0.3 for one over 5 m/s), *who it */
static int vehicles_ahead(Node *n, const float p[3], const float v[3], Node **who, float *slow, float *factor,
                          int all_round, float *t, int only_cars)
{
    uint8_t *ai = ai_of(n);
    float dt = g_clock_dt;
    CollIter it;
    for (Entity *e = coll_first(&it); e; e = coll_next(&it)) {
        Node *o = e->node;
        if (o == n) continue;
        if (only_cars ? o->cls != 1 : (o->cls != 1 && o->cls != 8)) continue;
        uint8_t *oa = ai_of(o);
        if (below(rdf(ai + 0x9d4c), rdf(oa + 0x9d40)) || below(rdf(oa + 0x9d4c), rdf(ai + 0x9d40)) ||
            below(rdf(ai + 0x9d54), rdf(oa + 0x9d48)) || below(rdf(oa + 0x9d54), rdf(ai + 0x9d48)))
            continue;
        if (!below(Pair_Dist(o, n), 100.0)) continue;   /* 0x4a5500 */
        float op[3] = {F32(o->frame.pos[0]), F32(o->frame.pos[1]), F32(o->frame.pos[2])};
        float ov[3], osp;
        Class_Velocity(o, ov, &osp);
        float q[3];
        for (int i = 0; i < 3; i++) q[i] = F32(ADD(MUL(dt, ov[i]), op[i]));
        if (!all_round) {
            float d[3];
            for (int i = 0; i < 3; i++) d[i] = F32(SUB(q[i], p[i]));
            double dotx = ADD(ADD(MUL(v[0], d[0]), MUL(v[1], d[1])), MUL(v[2], d[2]));
            float dot = F32(dotx);
            if (below(dotx, 0.0f)) continue;   /* 0x4a53bc */
            double vv = ADD(ADD(MUL(v[0], v[0]), MUL(v[1], v[1])), MUL(v[2], v[2]));
            double dd = ADD(ADD(MUL(d[1], d[1]), MUL(d[2], d[2])), MUL(d[0], d[0]));
            if (below(MUL(dot, dot), MUL(MUL(vv, dd), 0.25f))) continue;   /* 0x4a5404 */
        }
        float tc = closest(p, v, q, ov);
        *t = tc;
        if (ubits(tc) > 0x80000000u) tc = 0;
        else if (bits(tc) > 0x40400000) tc = 3.0f;
        float c[3], x[3], z[3], pts[10][3];
        memcpy(z, &o->frame.m[6], sizeof z);
        memcpy(x, &o->frame.m[0], sizeof x);
        for (int i = 0; i < 3; i++) c[i] = F32(ADD(MUL(tc, ov[i]), q[i]));
        for (int k = 0; k < 10; k++) probe_point(k, c, x, z, pts[k]);
        Frame f = n->frame, inv;
        f.pos[0] = ADD(MUL(tc, v[0]), p[0]);
        f.pos[2] = ADD(MUL(tc, v[2]), p[2]);
        Frame_Invert(&inv, &f);
        Frame_Points(pts, (const float(*)[3])pts, 10, &inv);
        for (int k = 0; k < 10; k++)
            if (below(fabsf(pts[k][2]), 2.5) && below(fabsf(pts[k][0]), 1.2999999523162842)) {   /* 0x4a5508, 0x4a5510 */
                *slow = 1.0f;
                if (bits(osp) < 0x40a00000) *factor = 0.5f <= tc ? 0.5f : tc;
                else *factor = from(0x3e99999a);
                *who = o;
                return 1;
            }
    }
    return 0;
}

/* ---- the sweeps of a moving sphere (0x46a1d0, 0x476610) ---- */

/* 0x46a1d0: a sphere at p moving v against one at q moving w (all of them nearly still: 0): the time of
   the closest approach within [0, tmax], 0 when not ahead; within r then, 1 with the offset between
   them (*t written either way) */
int Sphere_Sweep(const float p[3], const float v[3], const float q[3], const float w[3], float r,
                        float tmax, float *t, float off[3])
{
    const float k = 9.99999905104687e-09f;   /* 0x4a64e0 */
    if (below(MUL(v[0], v[0]), k) && below(MUL(v[1], v[1]), k) && below(MUL(v[2], v[2]), k) &&
        below(MUL(w[0], w[0]), k) && below(MUL(w[1], w[1]), k) && below(MUL(w[2], w[2]), k))
        return 0;
    float d[3], u[3];
    for (int i = 0; i < 3; i++) d[i] = F32(SUB(q[i], p[i]));
    for (int i = 0; i < 3; i++) u[i] = F32(SUB(v[i], w[i]));
    double lx = ADD(ADD(MUL(u[1], u[1]), MUL(u[2], u[2])), MUL(u[0], u[0]));
    float l = F32(lx), s;
    if (below(lx, 9.999999747378752e-05f)) s = tmax;   /* 0x4a64e4 */
    else {
        double sx = DIV(ADD(ADD(MUL(d[1], u[1]), MUL(d[2], u[2])), MUL(d[0], u[0])), l);
        s = F32(sx);
        if (!below_eq(sx, tmax)) s = tmax;
    }
    if (bits(s) < 1) s = 0;
    else
        for (int i = 0; i < 3; i++) d[i] = F32(SUB(d[i], MUL(s, u[i])));
    *t = s;
    if (below(MUL(r, r), ADD(ADD(MUL(d[1], d[1]), MUL(d[2], d[2])), MUL(d[0], d[0])))) return 0;
    memcpy(off, d, sizeof d);
    return 1;
}

/* 0x4770f0: one slab [lo, hi] crossed by p moving v: the entry and exit times (all time inside it when
   still within it), 0 when outside and still or the exit is past */
static int slab(float p, float v, float lo, float hi, float *t0, float *t1)
{
    float a, b;
    if (tiny(v)) {
        if (below(p, lo) || below(hi, p)) return 0;
        a = -1e30f, b = 1e30f;
    } else {
        a = F32(DIV(SUB(lo, p), v));
        b = F32(DIV(SUB(hi, p), v));
    }
    if (below_eq(a, b)) *t0 = a, *t1 = b;
    else *t0 = b, *t1 = a;
    return ubits(*t1) <= 0x80000000u;
}

/* 0x476df0: a point p moving v into the box [lo, hi] grown by m: the entry time within this frame (*t),
   and the normal of the face it enters by (against the motion along that axis) */
int Box_Sweep(const float p[3], const float v[3], const float lo[3], const float hi[3], float *t, float nrm[3],
              float m)
{
    float in, out, a, b;
    if (!slab(p[0], v[0], F32(SUB(lo[0], m)), F32(ADD(m, hi[0])), &in, &out)) return 0;
    if (!slab(p[2], v[2], F32(SUB(lo[2], m)), F32(ADD(m, hi[2])), &a, &b)) return 0;
    int axis = below(in, a) ? 1 : 0;
    if (below(in, a)) in = a;
    if (below(b, out)) out = b;
    if (!slab(p[1], v[1], F32(SUB(lo[1], m)), F32(ADD(m, hi[1])), &a, &b)) return 0;
    if (below(in, a)) axis = 2, in = a;
    if (below(b, out)) out = b;
    if (!(below_eq(in, out) && ubits(out) <= 0x80000000u)) return 0;
    if (!below_eq(in, g_clock_dt)) return 0;
    if (ubits(in) > 0x80000000u) return 0;
    *t = in;
    nrm[0] = nrm[1] = nrm[2] = 0;
    if (axis == 0) nrm[0] = bits(v[0]) > 0 ? -1.0f : 1.0f;
    else if (axis == 1) nrm[2] = bits(v[2]) > 0 ? -1.0f : 1.0f;
    else nrm[1] = bits(v[1]) > 0 ? -1.0f : 1.0f;
    return 1;
}

/* a vector into a frame's axes (the transposed rotation) and back */
static void to_local(float out[3], const float v[3], const Frame *f)
{
    const float *m = f->m;
    out[0] = F32(ADD(ADD(MUL(m[1], v[1]), MUL(m[2], v[2])), MUL(m[0], v[0])));
    out[1] = F32(ADD(ADD(MUL(m[3], v[0]), MUL(m[5], v[2])), MUL(m[4], v[1])));
    out[2] = F32(ADD(ADD(MUL(m[7], v[1]), MUL(m[6], v[0])), MUL(m[8], v[2])));
}

/* 0x4766b0: the entity's COLP box (0x47a810: the record's three mins and the maxima its counts pick) in
   its frame, swept by the segment (in place, in local space); the normal back in the world */
void Colp_Box(const Entity *e, float lo[3], float hi[3])
{
    const int32_t *r = e->a;
    lo[0] = lo[1] = lo[2] = hi[0] = hi[1] = hi[2] = 0;
    if (r) {
        const float *f = (const float *)r;
        lo[0] = f[3], lo[1] = f[8], lo[2] = f[11];
        hi[0] = f[r[0] + 2], hi[1] = f[r[1] + 6], hi[2] = f[r[2] + 10];
    }
}

static int colp_sweep(const float p[3], const float v[3], Entity *e, float *t, float nrm[3])
{
    float lo[3], hi[3];
    Colp_Box(e, lo, hi);
    Frame inv;
    Frame_Invert(&inv, &e->node->frame);
    float lp[3], lv[3];
    Frame_Points(&lp, (const float(*)[3])p, 1, &inv);
    to_local(lv, v, &e->node->frame);
    float n[3];
    if (!Box_Sweep(lp, lv, lo, hi, t, n, 0)) return 0;
    const float *m = e->node->frame.m;
    nrm[0] = F32(ADD(ADD(MUL(m[6], n[2]), MUL(m[3], n[1])), MUL(n[0], m[0])));
    nrm[1] = F32(ADD(ADD(MUL(m[1], n[0]), MUL(m[7], n[2])), MUL(m[4], n[1])));
    nrm[2] = F32(ADD(ADD(MUL(m[5], n[1]), MUL(m[2], n[0])), MUL(m[8], n[2])));
    return 1;
}

/* 0x479340, 0x479600, 0x4798c0 (axes u, w: x and z, x and y, y and z): the point (pu, pw) within the
   convex face seen along its third axis. The corners must lie on every side of it; then along the point's
   row, an edge on each side of it (crossing at it, or along the row and spanning it) is inside, two on one
   side outside. An edge whose current corner is on the row is passed over. */
static int face_holds(const CollFace *f, const float (*vs)[3], float pu, float pw, int u, int w)
{
    int n = f->n, sides = 0;
    for (int i = n; i && sides != 0xf; i--) {
        const float *q = vs[f->refs[i - 1]];
        sides |= pu < q[u] ? 2 : 1;
        sides |= pw < q[w] ? 8 : 4;
    }
    if (sides != 0xf) return 0;
    int left = 0, right = 0;
    const float *c = vs[f->refs[0]];
    for (int i = n - 1; i >= 0; c = vs[f->refs[i]], i--) {
        const float *b = vs[f->refs[i]];
        if (below(b[w], pw) && below(c[w], pw)) continue;
        if (b[w] > pw && c[w] > pw) continue;
        if (!(b[w] != pw)) {   /* equal or unordered */
            if (!(c[w] != pw)) {   /* along the row */
                if (below(b[u], pu) && below(c[u], pu)) return 0;
                if (below_eq(b[u], pu) || below_eq(c[u], pu)) return 1;
                return 0;
            }
            continue;
        }
        int side;   /* 1 left, 2 right */
        if (below(b[u], pu) && below(c[u], pu)) side = 1;
        else if (b[u] > pu && c[u] > pu) side = 2;
        else {
            double x = ADD(SUB(MUL(DIV(SUB(c[u], b[u]), SUB(c[w], b[w])), SUB(pw, b[w])), pu), b[u]);
            if (below(x, 0.0f)) side = 1;
            else if (bits(F32(x)) <= 0) return 1;   /* through the point */
            else side = 2;
        }
        if (side == 1) {
            if (left) return 0;
            if (right) return 1;
            left = 1;
        } else {
            if (right) return 0;
            if (left) return 1;
            right = 1;
        }
    }
    return 0;
}

/* 0x479160: the segment p + v through the face's plane from its front within the frame (the plane facing
   it, at least 1e-4), the point within the face (0x479290: seen along its normal's largest axis) */
static int face_sweep(const float p[3], const float v[3], const CollFace *f, const float (*vs)[3], float *t,
                      float nrm[3])
{
    const float *pl = f->plane;
    double den = ADD(ADD(MUL(pl[1], v[1]), MUL(pl[2], v[2])), MUL(pl[0], v[0]));
    if (den > exe_f32(0x4a6864)) return 0;
    float denf = F32(den);
    double num = ADD(ADD(ADD(MUL(pl[1], p[1]), MUL(pl[2], p[2])), MUL(pl[0], p[0])), f->d);
    if (below_eq(num, exe_f32(0x4a6860))) return 0;
    float numf = F32(num), tt = F32(-DIV(numf, denf));
    if (below(g_clock_dt, tt)) return 0;
    float q[3];
    for (int i = 0; i < 3; i++) q[i] = F32(ADD(MUL(v[i], tt), p[i]));
    float a = fabsf(pl[0]), b = fabsf(pl[1]), c = fabsf(pl[2]);
    int in;
    if (b > a) in = b > c ? face_holds(f, vs, q[0], q[2], 0, 2) : face_holds(f, vs, q[0], q[1], 0, 1);
    else in = a > c ? face_holds(f, vs, q[1], q[2], 1, 2) : face_holds(f, vs, q[0], q[1], 0, 1);
    if (!in) return 0;
    *t = tt;
    memcpy(nrm, pl, 12);
    return 1;
}

/* 0x479070: the mesh's earliest face met (not before the start) */
int Mesh_SweepFaces(const float p[3], const float v[3], const CollMesh *m, float *t, float nrm[3])
{
    float best = 1e30f, bn[3] = {0}, tt, n[3];
    for (const CollFace *f = m->faces; f; f = f->next)
        if (face_sweep(p, v, f, (const float (*)[3])m->verts, &tt, n) && ubits(tt) < 0x80000001u && tt < best) {
            best = tt;
            memcpy(bn, n, sizeof bn);
        }
    if (bits(best) > 0x7149f2c9) return 0;
    *t = best;
    memcpy(nrm, bn, sizeof bn);
    return 1;
}

/* 0x4768c0: the node and its siblings, each in its frame inside parent: unless of collision type 0x1000,
   its sphere (radius x 0.75 for 0x5000) grown by r swept first; a hit there counts for types 0x4000 and
   0x5000, and then (unless faces were asked of a 0x3000 mesh, 0x479070) its box grown by r swept in its
   frame; its children too. The earliest hit's node, time and normal */
static int tree_sweep(const float p[3], const float v[3], Node *node, const Frame *parent, Node **who, float *t,
                      float nrm[3], float r, int faces)
{
    int hit = 0;
    float best = from(0x7149f2ca), bn[3] = {0}, tt, lv3[3];
    Node *bw = NULL;
    for (; node; node = node->next) {
        Frame f;
        Frame_Mul(&f, &node->frame, parent);
        uint32_t type = node->flags & 0xf000;
        if (type != 0x1000) {
            float c[3], w[3];
            static const float zero[3];
            Frame_Points(&c, (const float(*)[3])node->centre, 1, &f);
            Point_Velocity(w, zero, node);
            float rad = node->radius;
            if (type == 0x5000) rad = F32(MUL(rad, 0.75f));   /* 0x4a6838 */
            if (Sphere_Sweep(p, v, c, w, F32(ADD(rad, r)), g_clock_dt, &tt, lv3)) {
                if (type == 0x4000 || type == 0x5000) {
                    hit = 1;
                    if (!below_eq(best, tt)) best = tt, memcpy(bn, lv3, sizeof bn), bw = node;
                }
                if (type == 0x3000 && node->coll && faces) {
                    if (Mesh_SweepFaces(p, v, node->coll, &tt, lv3)) {
                        hit = 1;
                        if (!below_eq(best, tt)) best = tt, memcpy(bn, lv3, sizeof bn), bw = node;
                    }
                } else {
                    Frame inv;
                    Frame_Invert(&inv, &f);
                    float lp[3], d[3], lv[3], n[3];
                    Frame_Points(&lp, (const float(*)[3])p, 1, &inv);
                    for (int i = 0; i < 3; i++) d[i] = F32(SUB(v[i], w[i]));
                    const float *m = inv.m;
                    lv[0] = F32(ADD(ADD(MUL(d[2], m[6]), MUL(d[1], m[3])), MUL(d[0], m[0])));
                    lv[1] = F32(ADD(ADD(MUL(d[2], m[7]), MUL(d[0], m[1])), MUL(d[1], m[4])));
                    lv[2] = F32(ADD(ADD(MUL(d[1], m[5]), MUL(d[0], m[2])), MUL(d[2], m[8])));
                    if (Box_Sweep(lp, lv, node->bmin, node->bmax, &tt, n, r)) {
                        hit = 1;
                        if (!below_eq(best, tt)) {
                            best = tt;
                            const float *g = f.m;
                            bn[0] = F32(ADD(ADD(MUL(n[2], g[6]), MUL(n[1], g[3])), MUL(n[0], g[0])));
                            bn[1] = F32(ADD(ADD(MUL(n[2], g[7]), MUL(n[1], g[4])), MUL(n[0], g[1])));
                            bn[2] = F32(ADD(ADD(MUL(n[1], g[5]), MUL(n[2], g[8])), MUL(n[0], g[2])));
                            bw = node;
                        }
                    }
                }
            }
        }
        Node *cw;
        if (node->child && tree_sweep(p, v, node->child, &f, &cw, &tt, lv3, r, faces)) {
            hit = 1;
            if (!below_eq(best, tt)) best = tt, memcpy(bn, lv3, sizeof bn), bw = cw;
        }
    }
    if (hit) {
        *t = best;
        memcpy(nrm, bn, sizeof bn);
        *who = bw;
    }
    return hit;
}

/* 0x476610: the segment against an entity by its collision kind (+0x14): 1 its COLP box, 2 always, 3 its
   node tree (0x476870, from the identity frame 0x4db298) */
int Entity_Sweep(const float p[3], const float v[3], Entity *e, Node **who, float *t, float nrm[3], float r,
                 int faces)
{
    switch (e->has_a) {
    case 1:
        if (!colp_sweep(p, v, e, t, nrm)) return 0;
        *who = e->node;
        return 1;
    case 2:
        *who = e->node;
        return 1;
    case 3: {
        if (!e->node) return 0;
        Frame id;
        memcpy(&id, exe_data(0x4db298, sizeof id), sizeof id);
        return tree_sweep(p, v, e->node, &id, who, t, nrm, r, faces);
    }
    }
    return 0;
}

/* ---- the objects ahead (0x4158c0) ---- */

/* 0x4158c0: the static entities (not vehicles, not class 4) whose ground box meets the look-ahead box,
   whose sphere (radius + 3) the segment p + v (its 6 floats) meets within the frame (0x46a1d0, against
   the entity's centre and velocity, 0x471b30); for roads (11) only those not facing within 0.8 of the
   vehicle's axis, for bridges (12) within 0.9 either way; then swept closely (0x476610, 3 m): *slow 1,
   *factor 1.25, *who, *t the closest approach's time of the two bodies' centres */
static int objects_ahead(Node *n, float *slow, float *factor, Node **who, const float seg[6], float *t)
{
    float dt = g_clock_dt;
    uint8_t *ai = ai_of(n);
    const float *z = &n->frame.m[6];
    CollIter it;
    for (Entity *e = coll_first(&it); e; e = coll_next(&it)) {
        Node *o = e->node;
        if (is_vehicle(o) || o->cls == 4) continue;
        const float *box = (const float *)&e->rest[(0x50 - 0x34) / 4];   /* 0x469f30 */
        if (below(box[3], rdf(ai + 0x9d40)) || below(rdf(ai + 0x9d4c), box[0]) || below(box[5], rdf(ai + 0x9d48)) ||
            below(rdf(ai + 0x9d54), box[2]))
            continue;
        float r = F32(ADD(e->r, 3.0f));   /* 0x4a53b8 */
        float oc[3], ov[3], osp, tt, off[3];
        Frame_Points(&oc, (const float(*)[3])e->c, 1, &o->frame);   /* 0x4240e0 */
        Class_Velocity(o, ov, &osp);
        if (!Sphere_Sweep(seg, seg + 3, oc, ov, r, dt, &tt, off)) continue;
        if (o->cls == 0xb || o->cls == 0xc) {
            const float *g = &o->frame.m[6];
            double dot = ADD(ADD(MUL(g[0], z[0]), MUL(g[1], z[1])), MUL(g[2], z[2]));
            if (o->cls == 0xb ? !below_eq(dot, 0.800000011920929f) : !below_eq(fabs(dot), 0.8999999761581421))   /* 0x4a54e4, 0x4a54e8 */
                continue;
        }
        if (!Entity_Sweep(seg, seg + 3, e, who, t, off, 3.0f, 0)) continue;
        *slow = 1.0f;
        float mv[3], ms, wv[3], ws;
        Class_Velocity(n, mv, &ms);
        Node *w = e->node;
        Class_Velocity(w, wv, &ws);
        float dx = F32(SUB(n->frame.pos[0], w->frame.pos[0]));
        float dz = F32(SUB(n->frame.pos[2], F32(w->frame.pos[2])));
        float dy = F32(SUB(n->frame.pos[1], w->frame.pos[1]));
        float u[3];
        for (int i = 0; i < 3; i++) u[i] = F32(SUB(wv[i], mv[i]));
        double lx = ADD(ADD(MUL(u[1], u[1]), MUL(u[2], u[2])), MUL(u[0], u[0]));
        float l = F32(lx);
        if (below(fabs(lx), 0.01f)) *t = 1e7f;
        else *t = F32(DIV(ADD(ADD(MUL(u[1], dy), MUL(u[2], dz)), MUL(u[0], dx)), l));
        *factor = 1.25f;
        return 1;
    }
    *slow = 0;
    return 0;
}

/* ---- the shots ahead (0x4163d0) ---- */

/* a shot of either list against the vehicle at p moving at v. The shots' velocity is the vector 0x4a9360,
   which nothing writes: every shot is taken as standing still (a quirk, kept). Within 200 m in x and z,
   the time t the two come closest (d . r / r . r, r = -v, d = p - q; 1e7 for r . r below 0.01) goes to *t;
   with 0.001 <= t <= 3 (the float's bits as an integer) and the vehicle then within 5 m of the shot: *slow
   1, *factor t + 0.5. The two lists differ only in their sums' orders and roundings */
static int shot_near(const float p[3], const float v[3], const Node *s, int second, float *slow, float *factor,
                     float *t)
{
    float q[3] = {F32(s->frame.pos[0]), F32(s->frame.pos[1]), F32(s->frame.pos[2])};
    float lim = F32(200.0);   /* 0x4a5520, a double */
    if (!below_eq(fabs(SUB(p[0], q[0])), lim) || !below_eq(fabs(SUB(p[2], q[2])), lim)) return 0;
    float d[3], r[3];
    for (int i = 0; i < 3; i++) {   /* 0x416a10 */
        d[i] = F32(SUB(p[i], q[i]));
        r[i] = F32(SUB(0.0f, v[i]));
    }
    float tt;
    if (!second) {
        float l = F32(ADD(ADD(MUL(r[0], r[0]), MUL(r[2], r[2])), MUL(r[1], r[1])));
        if (below(fabs(l), 0.01f)) tt = 1e7f;   /* 0x4a53e4 */
        else tt = F32(DIV(ADD(ADD(MUL(d[0], r[0]), MUL(r[1], d[1])), MUL(r[2], d[2])), l));   /* 0x416a60 */
    } else {
        double l = ADD(ADD(MUL(r[0], r[0]), MUL(r[1], r[1])), MUL(r[2], r[2]));
        if (below(fabs(l), 0.01f)) tt = 1e7f;
        else tt = F32(DIV(ADD(ADD(MUL(d[0], r[0]), MUL(r[1], d[1])), MUL(r[2], d[2])), F32(l)));
    }
    *t = tt;
    int32_t bits;
    memcpy(&bits, &tt, 4);
    if (below(tt, F32(0.001)) || bits >= 0x40400001) return 0;   /* 0x4a5448, a double; 3.0 */
    float a[3], b[3], e[3];
    for (int i = 0; i < 3; i++) {   /* 0x414d50: the vehicle and the still shot at t */
        a[i] = F32(ADD(MUL(v[i], tt), p[i]));
        b[i] = F32(ADD(MUL(0.0f, tt), q[i]));
        e[i] = F32(SUB(a[i], b[i]));
    }
    double dist = second ? fpu_sqrt(ADD(ADD(MUL(e[0], e[0]), MUL(e[1], e[1])), MUL(e[2], e[2])))   /* 0x4169e0 */
                         : fpu_sqrt(ADD(ADD(MUL(e[0], e[0]), MUL(e[2], e[2])), MUL(e[1], e[1])));
    if (!below(dist, 5.0f)) return 0;   /* 0x4a5518 */
    *factor = F32(ADD(tt, 0.5f));       /* 0x4a54d4 */
    *slow = 1.0f;
    return 1;
}

/* 0x4163d0: the live shots of the ordnance kinds 12, 14-17 and 19: each kind's node's children, then its
   second list (+0x10), by shot_near; the first that comes near answers */
int AI_ShotsAhead(Node *n, float *slow, float *factor, float *t)
{
    float p[3] = {F32(n->frame.pos[0]), F32(n->frame.pos[1]), F32(n->frame.pos[2])}, v[3], sp;
    Class_Velocity(n, v, &sp);   /* 0x425be0 */
    for (int k = 0; k < 20; k++) {
        const Ordnance *o = &g_ordnance[k];
        int ty = o->type;
        if (ty != 12 && ty != 14 && ty != 15 && ty != 16 && ty != 17 && ty != 19) continue;
        if (o->node)
            for (const Node *s = o->node->child; s; s = s->next)
                if (shot_near(p, v, s, 0, slow, factor, t)) return 1;
        for (const Node *s = o->list1; s; s = s->next)
            if (shot_near(p, v, s, 1, slow, factor, t)) return 1;
    }
    return 0;
}

static Frame g_probe;   /* the last step's frame: its words stay on the stack (AI_FindWay's leftovers) */

float AI_ProbeWord(int k) { return g_probe.m[k]; }

int AI_Avoid(Node *n, float steer, float throttle, Node **who, float *slow, float *factor, int cars_only,
             int all_round, float *t)
{
    uint8_t *d = n->data;
    int32_t e0 = rd32(d + 0xe0), e4 = rd32(d + 0xe4);
    memcpy(d + 0xe0, &steer, 4);
    memcpy(d + 0xe4, &throttle, 4);
    uint32_t flags;
    float vel[3], speed, acc[3], w[3];
    Frame f;
    Vehicle_Probe(n, &flags, vel, &speed, acc, w, &f);
    g_probe = f;
    memcpy(d + 0xe0, &e0, 4);
    memcpy(d + 0xe4, &e4, 4);
    float seg[6] = {F32(f.pos[0]), F32(f.pos[1]), F32(f.pos[2])};
    if (!cars_only) {
        float sp = F32(fpu_sqrt(ADD(ADD(MUL(vel[1], vel[1]), MUL(vel[2], vel[2])), MUL(vel[0], vel[0]))));
        if (terrain_ahead(&f, seg, sp, factor, rdf(d + 0xe8), 0, t)) {
            *who = NULL;
            return 1;
        }
    }
    if (vehicles_ahead(n, seg, vel, who, slow, factor, all_round, t, cars_only)) return 1;
    if (!cars_only) {
        float q[3];
        q[0] = F32(ADD(MUL(f.m[3], -0.30000001192092896f), seg[0]));   /* 0x4a5528 */
        q[1] = F32(ADD(MUL(f.m[4], -0.30000001192092896f), seg[1]));
        double qz = ADD(MUL(f.m[5], -0.30000001192092896f), seg[2]);
        q[2] = F32(qz);
        memcpy(seg, q, sizeof q);
        float nr[3];
        Terrain_Normal(nr, q[0], qz);
        const float *z = &f.m[6];
        float u0 = F32(SUB(MUL(z[2], nr[1]), MUL(z[1], nr[2])));
        float u1 = F32(SUB(MUL(z[0], nr[2]), MUL(z[2], nr[0])));
        float u2 = F32(SUB(MUL(z[1], nr[0]), MUL(z[0], nr[1])));
        float v0 = -u0, v1 = -u1, v2 = -u2;
        float g[3];
        g[0] = F32(SUB(MUL(v2, nr[1]), MUL(nr[2], v1)));
        g[1] = F32(SUB(MUL(nr[2], v0), MUL(nr[0], v2)));
        g[2] = F32(SUB(MUL(nr[0], v1), MUL(nr[1], v0)));
        float fps = g_clock_inv > 10000.0f ? 10000.0f : g_clock_inv;   /* 0x4a5498 */
        double k = MUL(MUL(fps, 50.0f), rdf(d + 0xe8));                 /* 0x4a552c */
        for (int i = 0; i < 3; i++) seg[3 + i] = F32(MUL(g[i], k));
        if (objects_ahead(n, slow, factor, who, seg, t)) return 1;
    }
    int r = crt_rand() % 1000;
    uint8_t *ai = ai_of(n);
    if (below(ADD(MUL(SUB(1.0f, rdf(ai + 0xa820)), 150.0f), 850.0f), r) && AI_ShotsAhead(n, slow, factor, t)) {   /* 0x4a540c, 0x4a5530 */
        *who = NULL;
        return 1;
    }
    *slow = 0;
    *factor = 0.5f;
    return 0;
}

/* ---- finding a way round (0x416ef0) and being blocked (0x415690) ---- */

typedef struct {
    Node *n;
    uint8_t *d;
    float *out;
    int cars_only, all_round;
    float slow, factor;
} Way;

static int way_try(Way *w, float s, float t)
{
    Node *who;
    float tt;
    return AI_Avoid(w->n, s, t, &who, &w->slow, &w->factor, w->cars_only, w->all_round, &tt);
}

static void way_set(Way *w, float s, float t, float f)
{
    memcpy(w->d + 0xe0, &s, 4);
    memcpy(w->d + 0xe4, &t, 4);
    *w->out = f;
}

/* the speed eased a tenth toward the cruising speed (from the faster side) */
static void way_blend(Way *w)
{
    float c = AI_Cruise(w->n), ac = rdf(w->d + 0xac);
    double v = below(c, ac) ? ADD(MUL(ac, 0.9f), MUL(c, 0.1f)) : ADD(MUL(ac, 0.1f), MUL(c, 0.9f));   /* 0x4a54c0, 0x4a54ac */
    AI_Speed(w->n, F32(v));
}

void AI_FindWay(Node *n, uint8_t *ai, Node *who, float *factor, int cars_only, int all_round, float slow0,
                float factor0)
{
    uint8_t *d = n->data;
    Way w = {n, d, factor, cars_only, all_round, slow0, factor0};   /* the original leaves both unset */
    float best = from(0x7f7fffff), s0 = rdf(d + 0xe0), bs = s0, bf = 0.5f;
    float bt = 0;   /* the original's is left unset when neither throttle sweep runs */
    float lim = AI_SteerLimit(n);
    float v[3], sp;
    Class_Velocity(n, v, &sp);
    bool same;
    if (who && is_vehicle(who)) {
        const float *a = &who->frame.m[6], *b = &n->frame.m[6];
        same = !below_eq(ADD(ADD(MUL(a[2], b[2]), MUL(a[0], b[0])), MUL(a[1], b[1])), 0.7);   /* 0x4a5538 */
    } else
        same = bits(sp) > 0x41700000;   /* 15 */
    bool right = below(g_clock_game, rdf(ai + 0x9d34)) ? bits(s0) > 0 : ubits(s0) > 0x80000000u;
    if (below(fabs((double)s0), 0.05)) {   /* 0x4a5540: going straight: either way, wider and wider */
        AI_Speed(n, F32(ADD(MUL(AI_Cruise(n), 0.1f), MUL(rdf(d + 0xac), 0.9f))));
        bt = rdf(d + 0xe4);
        float s = 0.25f;
        if (bits(lim) >= 0x3e800000)
            for (;;) {
                int r = way_try(&w, s, bt);
                float val = F32(SUB(100.0f, w.factor));   /* 0x4a5548 */
                if (!r) return way_set(&w, s, bt, bf);
                if (!below(best, val)) best = val, bs = s, bf = w.factor;
                float ms = -s;
                r = way_try(&w, ms, bt);
                val = F32(SUB(100.0f, w.factor));
                if (!r) return way_set(&w, ms, bt, bf);
                if (!below(best, val)) best = val, bs = ms, bf = w.factor;
                double ns = ADD(s, 0.25f);   /* 0x4a5404 */
                s = F32(ns);
                if (!below_eq(ns, lim)) break;
            }
        *factor = bf;
        memcpy(d + 0xe0, &bs, 4);
        return;
    }
    float osp = 0, ov[3];
    if (who && is_vehicle(who)) Class_Velocity(who, ov, &osp);
    float full = right ? 1.0f : F32(Vehicle_Throttle(n, 1.0f));
    if (bits(osp) <= 0x40000000 || same) {
        way_blend(&w);
        bt = rdf(d + 0xe4);
    } else {
        /* the throttles: from 0 up by 0.2 (to 1 turning right, to the most that holds turning left), then
           from -0.2 down to the most braking that holds */
        float t = 0;
        if (right || ubits(full) <= 0x80000000u)
            for (;;) {
                if (!way_try(&w, s0, t)) return way_set(&w, s0, t, bf);
                if (!below_eq(best, w.slow)) best = w.slow, bt = t, bf = w.factor;
                double nt = ADD(t, 0.2f);   /* 0x4a53f4 */
                t = F32(nt);
                if (!below_eq(nt, full)) break;
            }
        float lo = F32(Vehicle_Throttle(n, -1.0f));
        t = from(0xbe4ccccd);
        if (ubits(lo) >= 0xbe4ccccdu)
            for (;;) {
                if (!way_try(&w, s0, t)) return way_set(&w, s0, t, bf);
                if (!below_eq(best, w.slow)) best = w.slow, bt = t, bf = w.factor;
                double nt = SUB(t, 0.2f);
                t = F32(nt);
                if (below(nt, lo)) break;
            }
    }
    /* the steering by 0.25 to the side it was turning first, then the other; at the end full lock the
       other way */
    float nl = -lim;
    for (int pass = 0; pass < 2; pass++) {
        bool up = right ? pass == 0 : pass == 1;
        double ns = up ? ADD(s0, 0.25f) : SUB(s0, 0.25f);
        float s = F32(ns);
        if (up ? !below_eq(ns, lim) : !below_eq(nl, s)) continue;
        for (;;) {
            if (!way_try(&w, s, bt)) return way_set(&w, s, bt, bf);
            if (!below(best, w.slow)) best = w.slow, bf = w.factor;
            ns = up ? ADD(s, 0.25f) : SUB(s, 0.25f);
            s = F32(ns);
            if (up ? !below_eq(ns, lim) : below(ns, nl)) break;
        }
    }
    way_set(&w, right ? lim : nl, bt, bf);
}

int AI_TerrainAhead(Node *n, const float p[3], float *slow, float *t)
{
    uint8_t *d = n->data;
    return terrain_ahead(&n->frame, p, rdf(d + 0xac), slow, rdf(d + 0xe8), 1, t);
}

int AI_Blocked(Node *n)
{
    uint8_t *d = n->data, *ai = ai_of(n);
    const float *z = &n->frame.m[6];
    double k = MUL(MUL(g_clock_inv, rdf(d + 0xe8)), 7.0f);   /* 0x4a5454 */
    float seg[6] = {F32(n->frame.pos[0]), F32(n->frame.pos[1]), F32(n->frame.pos[2]), F32(MUL(z[0], k)),
                    F32(MUL(z[1], k)), F32(MUL(k, z[2]))};
    float slow;
    if (terrain_ahead(&n->frame, seg, rdf(d + 0xac), &slow, rdf(d + 0xe8), 1, (float *)(ai + 0x9d28))) return 1;
    float dt = g_clock_dt;
    CollIter it;
    for (Entity *e = coll_first(&it); e; e = coll_next(&it)) {
        if (e->node == n) continue;
        float r = F32(ADD(e->r, 3.0f)), oc[3], ov[3], osp, t, off[3];
        Frame_Points(&oc, (const float(*)[3])e->c, 1, &e->node->frame);   /* 0x471b30 */
        Class_Velocity(e->node, ov, &osp);
        if (!Sphere_Sweep(seg, seg + 3, oc, ov, r, dt, &t, off)) continue;
        Node *who;
        if (Entity_Sweep(seg, seg + 3, e, &who, &t, off, 0, 0)) return 1;
    }
    return 0;
}

/* ---- the line of sight (0x413570) ---- */

int AI_CanSee(Node *a, Node *b)
{
    if (!a || !b) return 0;   /* the original reads through them: the scripts never pass none */
    float dt = g_clock_dt, p[3], q[4], seg[6];
    Node_Centre(q, a);   /* 0x4223e0 */
    memcpy(p, q, sizeof p);
    p[1] = F32(ADD(p[1], 1.5f));   /* 0x4a53a8 */
    Node_Centre(q, b);
    q[1] = F32(ADD(q[1], 1.5f));
    float inv = g_clock_inv;
    memcpy(seg, p, sizeof p);
    for (int i = 0; i < 3; i++) seg[3 + i] = F32(MUL(F32(SUB(q[i], p[i])), inv));
    double eye[3] = {a->frame.pos[0], ADD(a->frame.pos[1], 1.5), a->frame.pos[2]};   /* 0x4a53b0 */
    if (Terrain_Line(eye, q)) return 0;
    CollIter it;
    for (Entity *e = coll_first(&it); e; e = coll_next(&it)) {
        Node *o = e->node, *who;
        if (is_vehicle(o) || o == b) continue;
        float r = F32(ADD(e->r, 3.0f));   /* 0x4a53b8 */
        float oc[3], ov[3], osp, t, off[3];
        Frame_Points(&oc, (const float(*)[3])e->c, 1, &o->frame);   /* 0x471b30 */
        Class_Velocity(o, ov, &osp);
        if (!Sphere_Sweep(seg, seg + 3, oc, ov, r, dt, &t, off)) continue;
        if (Entity_Sweep(seg, seg + 3, e, &who, &t, off, 3.0f, 0)) return 0;
    }
    return 1;
}
