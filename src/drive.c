/* The AI's driving controls (drive.h). In the current rounding mode (render/fpu.h). */
#include "drive.h"
#include "ai.h"
#include "clock.h"
#include "entity.h"
#include "exe.h"
#include "fsm.h"
#include "math3d.h"
#include "orders.h"
#include "physics.h"
#include "platform.h"
#include "world.h"
#include "render/fpu.h"
#include <math.h>
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
static uint32_t ubits(float f) { return (uint32_t)bits(f); }
static uint8_t *ai_of(const uint8_t *d) { return *Data_Ptr((uint8_t *)d, 0x108); }   /* 0x42d380 */
static uint8_t *wheel(uint8_t *d, uint32_t off) { return ((Node *)*Data_Ptr(d, off))->data; }
static bool is_vehicle(const Node *n) { return n->cls == 1 || n->cls == 8 || n->cls == 9; }   /* 0x42e570 */

/* the "1 / sqrt" of the original's direction helpers: 1 / sqrt of a float length squared, or the stand-in
   when it is tiny (the bounds as bits, as the original tests them) */
static bool tiny(double l2x, float l2) { return !(l2x <= -9.999999747378752e-05) && bits(l2) < 0x38d1b717; }

float AI_HeadingNow(Node *n)
{
    float x = n->frame.m[6], y = n->frame.m[7], z = n->frame.m[8];
    (void)y;
    if (fabsf(x) == 0 && fabsf(z) == 0) return 0;
    if (is_vehicle(n)) {
        float dir = rdf(n->data + 0xe8);
        x = F32(MUL(x, dir));
        z = F32(MUL(dir, z));
    }
    return F32(atan2(x, z));   /* fpatan */
}

double Angle_Diff(float a, float b)
{
    double dx = SUB(b, a);
    float d = F32(dx);
    if (dx > 3.1415927410125732) return SUB(d, 6.2831854820251465);   /* 0x4a53c0, 0x4a53c4 */
    if (ubits(d) > 0xc0490fdbu) return ADD(d, 6.2831854820251465);
    return d;
}

double Dir_Cos(float ax, float az, float bx, float bz, float px, float pz)
{
    float dx = F32(SUB(bx, ax));
    double dzx = SUB(bz, az);
    float dz = F32(dzx);
    double l2x = ADD(MUL(dzx, dz), MUL(dx, dx));
    float l2 = F32(l2x);
    if (tiny(l2x, l2)) return 1.0;
    double inv1x = Fast_InvSqrt(l2);
    if (!(inv1x <= 100.0)) return 1.0;   /* 0x4a5278 */
    float inv1 = F32(inv1x);
    float ux = F32(MUL(inv1, dx)), uz = F32(MUL(inv1, dz)), ex = F32(SUB(px, ax));
    double ezx = SUB(pz, az);
    float ez = F32(ezx);
    l2x = ADD(MUL(ex, ex), MUL(ezx, ez));
    l2 = F32(l2x);
    if (tiny(l2x, l2)) return 1.0;
    double inv2x = Fast_InvSqrt(l2);
    if (!(inv2x <= 100.0f)) return 1.0;   /* 0x4a5280 */
    float inv2 = F32(inv2x);
    return ADD(MUL(MUL(inv2, ez), uz), MUL(MUL(inv2, ex), ux));
}

float AI_SteerLimit(Node *n)
{
    if (!n->data || !is_vehicle(n)) return 1.0f;   /* 0x4a6750 */
    if (View_Distance(n, 25.0f) == 2 || n->cls == 9) return 1.0f;   /* 0x42b810 */
    uint8_t *d = n->data, *susp = wheel(d, 0x3c8);
    float load = rdf(wheel(d, 0x3bc) + 0xc);
    load = F32(ADD(rdf(wheel(d, 0x3ac) + 0xc), load));
    load = F32(ADD(rdf(wheel(d, 0x3b8) + 0xc), load));
    double loadx = ADD(rdf(wheel(d, 0x3a8) + 0xc), load);
    load = F32(loadx);
    float cap = F32(MUL(loadx, 7.84f));   /* 0x4a67f8 */
    float sp = rdf(d + 0xac);
    float k = F32(MUL(MUL(MUL(MUL(Vehicle_TerrainGrip(d), rdf(d + 0x46c)), rdf(susp + 0x14)), load), 0.25f));
    float r;
    if (bits(sp) < 0x38d1b717) r = 1.0f;
    else {
        double tx = MUL(MUL(rdf(d + 0x110), sp), sp);
        float t = F32(tx);
        r = F32(DIV(k, tx <= cap ? t : cap));
    }
    if (bits(r) > 0x3f800000) r = 1.0f;
    return r;
}

/* 0x409fe0: class 9's steering, a turn rate toward a by the sine of the shorter way round x 0.15 */
static void heli_steer(Node *n, float a)
{
    uint8_t *d = n->data;
    float dt = g_clock_dt;
    if (bits(a) > 0x40490fdb) a = F32(SUB(a, 6.2831854820251465f));   /* 0x4a51d4 */
    else if (ubits(a) > 0xc0490fdbu) a = F32(ADD(a, 6.2831854820251465f));
    double hx = AI_Heading(n);
    float h = F32(hx), one, other;
    double dx = SUB(a, h);
    if (hx < a) {
        other = F32(dx);
        one = F32(SUB(dx, 6.2831854820251465f));
    } else {
        one = F32(dx);
        other = F32(ADD(dx, 6.2831854820251465f));
    }
    float x = fabsf(one) < fabsf(other) ? one : other;
    x = F32(-(double)x);
    x = F32(MUL(x, 0.15f));   /* 0x4a519c */
    double s = DIV(MUL(MUL(ADD(MUL(MUL(rdf(d + 0xe4), dt), 20.2f), rdf(d + 0xac)), dt), sin(x)),
                   MUL(MUL(dt, dt), 20.2f));   /* 0x4a51dc */
    putf(d + 0xe0, F32(s));
    if (ubits(rdf(d + 0xe0)) > 0xbf800000u) put32(d + 0xe0, (int32_t)0xbf7fbe77);
    else if (rd32(d + 0xe0) > 0x3f800000) put32(d + 0xe0, 0x3f7fbe77);
}

void AI_Steer(Node *n, float a)
{
    if (n->cls == 9) {
        heli_steer(n, a);
        return;
    }
    uint8_t *d = n->data, *susp = wheel(d, 0x3c8);
    if (rd32(d + 0x454) & 4) {
        put32(d + 0xe0, 0);
        return;
    }
    float dt = g_clock_dt;
    if (dt < 0.01f) put32(d + 0xe0, 0);   /* 0x4a51e8, and on */
    float sp = rdf(d + 0xac);
    if (ubits(sp) < 0xb8d1b717u && bits(sp) < 0x38d1b717) {
        put32(d + 0xe0, 0);
        return;
    }
    float want = F32(-Angle_Diff(AI_Heading(n), a));
    want = F32(MUL(rdf(ai_of(d) + 0xa820), want));
    float k = rdf(d + 0x110);
    float load = rdf(wheel(d, 0x3a8) + 0xc);
    load = F32(ADD(rdf(wheel(d, 0x3ac) + 0xc), load));
    load = F32(ADD(rdf(wheel(d, 0x3b8) + 0xc), load));
    load = F32(ADD(rdf(wheel(d, 0x3bc) + 0xc), load));
    float cap;
    if (rd32(d + 0x454) & 0x10000)
        cap = F32(MUL(MUL(MUL(MUL(Vehicle_TerrainGrip(d), rdf(d + 0x46c)), rdf(susp + 0x14)), load), 0.25f));
    else
        cap = F32(MUL(load, 7.84f));   /* 0x4a51b0 */
    if (bits(cap) < 0x3a83126f) {
        put32(d + 0xe0, 0);
        return;
    }
    float s;
    if (MUL(MUL(sp, sp), k) <= cap) s = F32(DIV(want, MUL(MUL(sp, dt), k)));
    else s = F32(DIV(MUL(want, sp), MUL(cap, dt)));
    float lim = AI_SteerLimit(n);
    if (ubits(s) > 0x80000000u) {
        double m = -(double)lim;
        if (!(m <= s)) s = F32(m);
        if (ubits(s) > 0xbf7ff972u) s = -0.9999f;
    } else {
        if (lim < s) s = lim;
        if (bits(s) > 0x3f7ff972) s = 0.9999f;
    }
    putf(d + 0xe0, F32(MUL(rdf(d + 0xe8), s)));
}

float AI_Cruise(Node *n)
{
    if (n->cls == 9) return 10.0f;   /* 0x4a51b4 */
    return rdf(ai_of(n->data) + 0xa994);
}

void AI_Speed(Node *n, float want)
{
    AI_TouchUser(n);
    uint8_t *d = n->data;
    if (n->cls == 9) {   /* the helicopters': the speed's square's drag and the step, x 0.0495 within +-0.999 */
        float sp = rdf(d + 0xac);   /* 0x425be0 (0x431d70) */
        float drag = F32(MUL(MUL(sp, sp), 0.00155f));   /* 0x4a5198 */
        double t = MUL(ADD(MUL(MUL(SUB(want, sp), g_clock_inv), 0.15f), drag), 0.049504946917295456f);   /* 0x4a519c, 0x4a51a0 */
        float thr;
        if (t < -1.0f) thr = -0.999f;   /* 0x4a51a4; 0xbf7fbe77 */
        else {
            thr = F32(t);
            if (bits(thr) > 0x3f800000) put32((uint8_t *)&thr, 0x3f7fbe77);
        }
        putf(d + 0xe4, thr);
        return;
    }
    float sp = rdf(d + 0xac);
    float acc = F32(MUL(SUB(want, sp), g_clock_inv));
    float gain = rdf(ai_of(d) + 0xa81c);
    if (ubits(acc) <= 0x80000000u) acc = F32(MUL(acc, gain));
    float drag = 0;
    if (bits(sp) >= 0x3f800000) {
        float a = F32(MUL(rdf(d + 0x46c), 0.25f));   /* 0x4a5190 */
        drag = F32(MUL(MUL(MUL(rdf(d + 0x120), sp), sp), -0.1f));   /* 0x4a5194 */
        float roll = rdf(g_world.lights + 0x14 * (uint32_t)rd32(d + 0x45c) + 4);   /* 0x48e340 */
        float s = rdf(wheel(d, 0x3a8) + 0x14);
        s = F32(ADD(rdf(wheel(d, 0x3ac) + 0x14), s));
        s = F32(ADD(rdf(wheel(d, 0x3bc) + 0x14), s));
        double sx = ADD(rdf(wheel(d, 0x3b8) + 0x14), s);
        drag = F32(SUB(drag, MUL(MUL(sx, roll), a)));
    }
    double netx = SUB(acc, drag);
    float net = F32(netx), thr;
    if (!(netx > 0)) {   /* braking: the brakes' force (+0x3cc's +0x10) */
        float b = rdf(wheel(d, 0x3cc) + 0x10);
        if (ubits(b) < 0xb8d1b717u && bits(b) < 0x38d1b717) thr = 1.0f;
        else thr = F32(DIV(net, MUL(b, 8.0f)));   /* 0x4a51c0 */
    } else {   /* the engine's torque (0x42fad0) x the gearing */
        Node *eng = *Data_Ptr(d, 0x3c4);
        float e = F32(MUL(MUL(rdf(eng->data + 0x10), rdf(d + 0xa8)), rdf(d + 0x128)));
        if (rd32(d + 0x454) & 0x800) e = F32(MUL(e, 5.0f));   /* 0x4a51c4 */
        if (bits(sp) > 0x3f800000) e = F32(MUL(rdf(d + 0xb0), e));
        if (bits(e) < 0x38d1b717) {
            int32_t m = 0x38d1b717;
            memcpy(&e, &m, 4);
        }
        thr = F32(DIV(net, e));
    }
    if (ubits(thr) > 0x80000000u) {
        double m = MUL(Vehicle_Throttle(n, -1.0f), 0.9998999834060669f);   /* 0x4a51c8 */
        if (!(m <= thr)) thr = F32(m);
    } else {
        double c = ADD(MUL(gain, 0.25f), 0.75f);   /* 0x4a51cc */
        if (c < thr) thr = F32(c);
        double m = MUL(Vehicle_Throttle(n, 1.0f), 0.9998999834060669f);
        if (m < thr) thr = F32(m);
    }
    putf(d + 0xe4, thr);
}

/* the path's points (0x14 each from +0xb0) */
static float px_(const uint8_t *ai, int i) { return rdf(ai + 0xb0 + i * 0x14); }
static float pz_(const uint8_t *ai, int i) { return rdf(ai + 0xb4 + i * 0x14); }

/* the script path's point i (the FSM arena's floats) */
static float spt(uint8_t *ai, int i, int k)
{
    uint32_t pts = (uint32_t)Fsm_Word((uint32_t)rd32(ai + 0x88) + 0x54);
    int32_t w = Fsm_Word(pts + (uint32_t)i * 12 + (uint32_t)k * 4);
    float f;
    memcpy(&f, &w, 4);
    return f;
}

void Path_Advance(uint8_t *ai)
{
    const Frame *f = *(const Frame **)Data_Ptr(ai, 0);
    float vx = F32(f->pos[0]), vz = F32(f->pos[2]);
    for (;;) {
        if (rd32(ai + 0x98)) return;
        int32_t i = rd32(ai + 0xa8);
        float dx = F32(SUB(px_(ai, i), vx)), dz = F32(SUB(pz_(ai, i), vz));
        bool more = false;
        if (rd32(ai + 0xac) - i == 1) {
            if (ADD(MUL(dx, dx), MUL(dz, dz)) < 100.0f) {   /* 0x4a5280 */
                put32(ai + 0x98, 1);
                put32(ai + 0x94, 1);
            }
        } else if (i == 0) {
            if (ADD(MUL(dx, dx), MUL(dz, dz)) < 725.0f) {   /* 0x4a52c0 */
                put32(ai + 0xa8, 1);
                more = true;
            }
        } else if (!(Dir_Cos(px_(ai, i), pz_(ai, i), px_(ai, i + 1), pz_(ai, i + 1), vx, vz) <= -0.1f)) {   /* 0x4a52c4 */
            put32(ai + 0xa8, rd32(ai + 0xa8) + 1);
            more = true;
        }
        if (more) continue;
        if (rd32(ai + 0x84) == 1) return;
        float x = F32(f->pos[0]), z = F32(f->pos[2]);
        int32_t at = rd32(ai + 0xa0);
        double ezx = SUB(z, spt(ai, at, 2));
        float ex = F32(SUB(x, spt(ai, at, 0))), ez = F32(ezx);
        if (ADD(MUL(ezx, ez), MUL(ex, ex)) < 1600.0f) {   /* 0x4a52c8 */
            put32(ai + 0x94, 1);
            put32(ai + 0xa0, at + 1);
            if (rd32(ai + 0xa4) == at + 1) {
                put32(ai + 0xa0, 0);
                put32(ai + 0x9c, 1);
            }
        }
        return;
    }
}

/* a point moved 5 m off its segment (d its direction), when side is set */
static void aside(float *x, float *z, float dx, double dzx, float dz, bool keep_dz)
{
    double l2x = keep_dz ? ADD(MUL(dzx, dz), MUL(dx, dx)) : ADD(MUL(dz, dz), MUL(dx, dx));
    float l2 = F32(l2x), inv = 0.2f;
    if (!tiny(l2x, l2)) inv = F32(Fast_InvSqrt(l2));
    float ux = F32(MUL(inv, dx));
    *x = F32(ADD(MUL(MUL(inv, dz), -5.0f), *x));   /* 0x4a52d4 */
    *z = F32(ADD(MUL(ux, 5.0f), *z));             /* 0x4a5288 */
}

static float aim_angle(double ax, double az)
{
    double a = atan2(ax, az);   /* fpatan */
    float r = F32(a);
    if (!(a > -7.0f) || bits(r) >= 0x40e00000) plat_log(exe_str(0x4a92c4));   /* the original's fatal */
    return r;
}

float Path_Aim(Node *n, uint8_t *ai, int side, float look)
{
    if (n->cls == 9) look = 24.0f;
    const Frame *f = *(const Frame **)Data_Ptr(ai, 0);
    int32_t i = rd32(ai + 0xa8), count = rd32(ai + 0xac);
    float x = px_(ai, i), z = pz_(ai, i);
    if (side) {
        float dx, dz;
        if (i < 1) dx = F32(SUB(px_(ai, i + 1), x)), dz = F32(SUB(pz_(ai, i + 1), z));
        else dx = F32(SUB(x, px_(ai, i - 1))), dz = F32(SUB(z, pz_(ai, i - 1)));
        aside(&x, &z, dx, dz, dz, false);
    }
    double ezx = SUB(z, f->pos[2]);
    float ez = F32(ezx), ex = F32(SUB(x, f->pos[0]));
    float d2 = F32(ADD(MUL(ezx, ez), MUL(ex, ex)));
    if (MUL(look, look) < d2 || count - i == 1) return aim_angle(ex, ez);
    float rem = F32(SUB(look, fpu_sqrt(d2)));
    float lx = x, lz = z;
    for (i++; i < count; i++) {
        x = px_(ai, i), z = pz_(ai, i);
        if (side) {
            float dx = F32(SUB(x, px_(ai, i - 1)));
            double dzx = SUB(z, pz_(ai, i - 1));
            aside(&x, &z, dx, dzx, F32(dzx), true);
        }
        float sx = F32(SUB(x, lx));
        double szx = SUB(z, lz);
        float sz = F32(szx);
        double segx = fpu_sqrt(ADD(MUL(szx, sz), MUL(sx, sx)));
        float seg = F32(segx);
        if (!(segx <= rem)) {
            x = F32(ADD(MUL(DIV(sx, seg), rem), lx));
            z = F32(ADD(MUL(DIV(sz, seg), rem), lz));
            return aim_angle(SUB(x, f->pos[0]), SUB(z, f->pos[2]));
        }
        rem = F32(SUB(rem, seg));
        lx = x, lz = z;
    }
    i = rd32(ai + 0xa8);
    return aim_angle(SUB(px_(ai, i), n->frame.pos[0]), SUB(pz_(ai, i), n->frame.pos[2]));
}
