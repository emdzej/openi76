/* A vehicle's simulation (physics.h). In the current rounding mode (render/fpu.h). */
#include "physics.h"
#include "ai.h"
#include "classes.h"
#include "collide.h"
#include "engine.h"
#include "entity.h"
#include "exe.h"
#include "terrain.h"
#include "weapons.h"
#include "world.h"
#include "render/fpu.h"
#include <math.h>
#include <string.h>

#define ADD fpu_add
#define SUB fpu_sub
#define MUL fpu_mul
#define DIV fpu_div
#define F32 fpu_f32

Node *g_taurus;              /* 0x505454: the script's actor named "taurus" (0x409b00, from 0x40c970) */
uint32_t g_game_options;     /* 0x5e14e4: the shell's; bit 0 puts the user's vehicle with Taurus */

static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }
static void put32(uint8_t *p, int32_t v) { memcpy(p, &v, 4); }
static float rdf(const uint8_t *p) { float v; memcpy(&v, p, 4); return v; }
static void putf(uint8_t *p, float v) { memcpy(p, &v, 4); }
static int below(double a, double b) { return a < b || a != a || b != b; }
static int below_eq(double a, double b) { return a <= b || a != a || b != b; }
static Node *slot(uint8_t *d, uint32_t off) { return *Data_Ptr(d, off); }

/* 0x42fe30: a wheel's ground height under its place, into its data's +0x2c and +0x30; +0x34 = 0 (the
   mass and 0.25 it is passed go unused) */
static void wheel_ground(Node *w)
{
    float p[3], h;
    Node_WorldPos(p, w);
    Ground_Height(p[0], p[1], p[2], &h, NULL);
    putf(w->data + 0x30, h);
    putf(w->data + 0x2c, h);
    put32(w->data + 0x34, 0);
}

/* 0x4304f0: the brakes' force (+0xc and +0x10); in a network game scaled instead */
static void brakes_set(Node *b, float f)
{
    putf(b->data + 0x10, f);
    putf(b->data + 0xc, f);
}

void Vehicle_PhysInit(Node *n)
{
    float h;
    Ground_Height(F32(n->frame.pos[0]), F32(n->frame.pos[1]), F32(n->frame.pos[2]), &h, NULL);
    n->frame.pos[1] = h;
    uint8_t *d = n->data;   /* 0x42d5e0 */
    uint8_t *engine = slot(d, 0x3c4)->data;
    /* the wheels: front pair 0x3ac and 0x3a8, rear 0x3bc and 0x3b8, the middle pair if there is one */
    static const uint32_t wheels[6] = {0x3ac, 0x3a8, 0x3bc, 0x3b8, 0x3b4, 0x3b0};
    for (int i = 0; i < 6; i++)
        if (i < 4 || slot(d, wheels[i])) wheel_ground(slot(d, wheels[i]));
    /* the track (+0x114, across the front pair) and the wheelbase (+0x118, front to rear) */
    float a[3], b[3], v[3];
    Node_WorldPos(a, slot(d, 0x3ac));
    Node_WorldPos(b, slot(d, 0x3a8));
    for (int k = 0; k < 3; k++) v[k] = F32(SUB(a[k], b[k]));
    putf(d + 0x114, F32(fpu_sqrt(ADD(ADD(MUL(v[1], v[1]), MUL(v[2], v[2])), MUL(v[0], v[0])))));
    Node_WorldPos(b, slot(d, 0x3bc));
    v[0] = F32(SUB(a[0], b[0]));
    v[1] = F32(SUB(a[1], b[1]));
    double dz = SUB(a[2], b[2]);   /* squared off the stack by its float */
    v[2] = F32(dz);
    putf(d + 0x118, F32(fpu_sqrt(ADD(ADD(MUL(dz, v[2]), MUL(v[1], v[1])), MUL(v[0], v[0])))));
    putf(d + 0x110, F32(DIV(1.0f, MUL(rdf(d + 0x118), 2.0f))));   /* 0x4a6750, 0x4a674c */
    putf(d + 0x11c, F32(slot(d, 0x3bc)->frame.pos[2]));
    put32(d + 0x478, 0x3dcccccd);   /* 0.1 */
    /* the body's four corners at half its height, from the COLP record's box, and its +0x28 */
    Entity *e = Entity_OfNode(n);
    *Data_Ptr(d, 0) = e;
    const uint8_t *c = e->a;
    float y = F32(MUL(ADD(rdf(c + 0x24), rdf(c + 0x20)), 0.5f));   /* 0x4a6758 */
    static const uint8_t corner[4][2] = {{0xc, 0x2c}, {0x18, 0x2c}, {0x18, 0x38}, {0xc, 0x38}};
    for (int i = 0; i < 4; i++) {
        memcpy(d + 4 + 0xc * i, c + corner[i][0], 4);
        putf(d + 8 + 0xc * i, y);
        memcpy(d + 0xc + 0xc * i, c + corner[i][1], 4);
    }
    memcpy(d + 0x34, c + 0x28, 4);
    static const uint16_t zero[] = {0x48c, 0x488, 0x504, 0x500, 0x494, 0x490, 0x49c, 0x498, 0x4ac, 0x4a8,
                                    0x4cc, 0x4c8, 0x4e8, 0x4a0, 0x4dc, 0x4b4, 0x4bc, 0x4b8, 0x4d4, 0x4d0};
    for (size_t i = 0; i < sizeof zero / sizeof zero[0]; i++) put32(d + zero[i], 0);
    /* half the wheelbase (w) and the track (t), signed, into the wheels' contact table */
    static const struct { uint16_t off; uint8_t track, neg; } half[] = {
        {0x4ec, 0, 1}, {0x4a4, 0, 0}, {0x4d8, 1, 0}, {0x4b0, 1, 1}, {0x4c0, 1, 1}, {0x4c4, 0, 0},
        {0x4e0, 1, 0}, {0x4e4, 0, 0}, {0x4f0, 1, 1}, {0x4f4, 0, 1}, {0x4f8, 1, 0}, {0x4fc, 0, 1}};
    for (size_t i = 0; i < sizeof half / sizeof half[0]; i++)
        putf(d + half[i].off,
             F32(MUL(rdf(d + (half[i].track ? 0x114 : 0x118)), half[i].neg ? -0.5f : 0.5f)));   /* 0x4a6760 */
    /* the specials (0x42d920: the node's data's first word is its kind): 3 a stronger body, 8 faster guns */
    for (int i = 0; i < 3; i++) {
        Node *s = slot(d, 0x3ec + 4 * (uint32_t)i);
        int kind = s ? rd32(s->data) : 0;
        if (kind == 3) put32(d + 0x128, 0x3fa00000);   /* 1.25 */
        else if (kind == 8) Weapons_Faster(n);
    }
    put32(d + 0x454, rd32(d + 0x454) | 0x1001);
    if (g_taurus == n || ((g_game_options & 1) && (n->flags & 0x10))) put32(d + 0x454, rd32(d + 0x454) | 0x10000);
    put32(d + 0x45c, Terrain_Class(n->frame.pos[0], n->frame.pos[2]));
    Body_Set(d + 0xa4, rdf(d + 0xa4), rdf(d + 0xa4));
    putf(d + 0xb4, F32(DIV(exe_f32(0x4a6768), rdf(d + 0x118))));
    brakes_set(slot(d, 0x3cc), F32(MUL(rdf(d + 0xa8), 2300.0f)));   /* 0x4a676c */
    Engine_Step(engine, 0, rdf(d + 0xac), (uint32_t)rd32(d + 0x454), 0, rd32(d + 0x104));   /* 0x42d4f0 */
    /* 0x4761b0: the engine's sound (0x476240); no audio yet */
}

/* 0x429eb0: a vehicle's death (the wreck, the explosions, the score and the messages); not ported yet */
static void vehicle_die(Node *n) { (void)n; }

void Vehicle_Kill(Node *n)
{
    if (!n || n->cls != 1) return;
    uint8_t *d = n->data;
    if (rd32(d + 0x454) & 0x8020) return;
    put32(d + 0x454, rd32(d + 0x454) | 0x20);
    vehicle_die(n);
}

/* 0x4683c0: a sound at the node (4: the vehicle righted); no audio yet */
static void sound(int id, Node *n) { (void)id, (void)n; }

int Vehicle_Upright(Node *n)
{
    uint8_t *d = n->data;
    float v[3], up[3] = {n->frame.m[3], n->frame.m[4], n->frame.m[5]};
    memcpy(v, d + 0xc8, sizeof v);
    float dot = F32(ADD(ADD(MUL(rdf(d + 0x460), up[0]), MUL(rdf(d + 0x464), up[1])), MUL(rdf(d + 0x468), up[2])));
    /* turning slowly about x and z, and the up axis along the ground's normal (either way) */
    if (!below(fabsf(v[0]), 0.6283185307179586) || !below(fabsf(v[2]), 0.6283185307179586)) return 1;   /* 0x4a6618 */
    if (!(fabsf(dot) > 0.8f)) return 1;   /* 0x4a6620 */
    uint32_t flags = (uint32_t)rd32(d + 0x454), bits;
    memcpy(&bits, &dot, 4);
    if (!(flags & 0x24) && bits > 0x80000000u) {   /* upside down */
        if (n->flags & 0x20) {
            memset(d + 0xc8, 0, 12);
        } else if ((flags & 0x10000) /* && not a network game (0x461900) */) {
            /* the user's (or Taurus's) is put back on its wheels: the up and right axes reversed */
            for (int k = 0; k < 3; k++) n->frame.m[3 + k] = -n->frame.m[3 + k];
            for (int k = 0; k < 3; k++) n->frame.m[k] = -n->frame.m[k];
            sound(4, n);
        } else {
            Vehicle_Kill(n);
            memset(d + 0xc8, 0, 12);
        }
    }
    if (rd32(d + 0x454) & 0x200) {
        sound(4, n);
        put32(d + 0x454, rd32(d + 0x454) & ~0x200);
    }
    return 0;
}

void Vehicle_Steer(Node *n)
{
    uint8_t *d = n->data;
    float a = F32(MUL(rdf(d + 0xe0), exe_f32(0x4a67bc)));   /* 0x42d390; -pi/4 */
    Node_SetYaw(slot(d, 0x3ac), a);
    Node_SetYaw(slot(d, 0x3a8), a);
}

static float terrain_rough(int tclass) { return rdf(g_world.lights + 0x14 * (uint32_t)tclass + 8); }   /* 0x48e350 */

void Wheel_Contact(Node *w, int hold, float force, int tclass)
{
    uint8_t *d = w->data;
    float p[3], h;
    Node_WorldPos(p, w);
    Ground_Height(p[0], p[1], p[2], &h, NULL);
    putf(d + 0x2c, h);
    /* the ground's bumps: an 8 by 8 table by the wheel's whole metres, by the terrain's roughness */
    int ix = (int)(int64_t)p[0] & 7, iz = (int)(int64_t)p[2] & 7;   /* _ftol */
    float r = terrain_rough(tclass), bump = exe_f32(0x4a5ba8 + 4 * (uint32_t)(ix + iz * 8)), y;
    y = F32(ADD(MUL(r, bump), h));
    if (rd32(d + 0x44)) {
        int32_t rb;
        memcpy(&rb, &r, 4);
        if (rb < 0x3dcccccd) y = F32(ADD(MUL(bump, exe_f32(0x4a5ce4)), y));   /* below 0.1: 0.1 more */
    }
    /* the ground's rise since the last step, and the spring's target */
    float rise = F32(SUB(y, rdf(d + 0x30))), s;
    if (hold) s = rdf(d + 0x3c);
    else s = F32(ADD(MUL(SUB(rise, rdf(d + 0x34)), exe_f32(0x4a5ce8)), force));
    put32(d + 0x40, hold);
    if (!below_eq(rdf(d + 0x3c), s)) put32(d + 0x40, 1);
    putf(d + 0x30, y);
    putf(d + 0x34, rise);
    double f = Filter_Step(d + 0x58, s);
    float t = F32(f);
    if (below(f, rdf(d + 0x3c))) t = rdf(d + 0x3c);
    else if (below(rdf(d + 0x38), t)) t = rdf(d + 0x38);
    putf(d + 0x24, t);
    w->frame.pos[1] = ADD(rdf(d + 0x28), t);
}

/* 0x4301f0: a braking wheel on the ground lays a skid mark (0x46bcc0, a new one 0x46bc40 with +0x48 = 1)
   and, two times in three (rand), a puff of dust (0x45c780) behind it; +0x48 = 0 otherwise. Not ported
   yet (the marks and the particles): only +0x48 is kept */
static void wheel_skid(Node *w, int braking)
{
    if (!braking || rd32(w->data + 0x40)) put32(w->data + 0x48, 0);
    else if (!rd32(w->data + 0x48)) put32(w->data + 0x48, 1);   /* a new mark */
}

void Vehicle_Brake(Node *n, uint32_t flags, int braking)
{
    uint8_t *d = n->data;
    if (braking && !(flags & 0x8020)) put32(d + 0x454, rd32(d + 0x454) | 2);
    else put32(d + 0x454, rd32(d + 0x454) & ~2);
    wheel_skid(slot(d, 0x3bc), braking);
    wheel_skid(slot(d, 0x3b8), braking);
}

/* ---- the step's integrators ---- */

void Frame_Yaw(Frame *out, float a)
{
    float s = F32(sin(a)), c = F32(cos(a));   /* fsincos 0x452a70 */
    memcpy(out, exe_data(0x4db298, sizeof *out), sizeof *out);
    out->m[0] = c;
    out->m[2] = -s;
    out->m[6] = s;
    out->m[8] = c;
}

void Integrate(double p[3], float v[3], const float a[3], float dt)
{
    float ad[3], h[3];
    for (int k = 0; k < 3; k++) ad[k] = F32(MUL(dt, a[k]));
    for (int k = 0; k < 3; k++) h[k] = F32(ADD(MUL(ad[k], 0.5f), v[k]));   /* 0x4a66b0 */
    double x = MUL(h[0], dt);   /* x's product added unrounded, y's and z's as floats */
    float y = F32(MUL(dt, h[1])), z = F32(MUL(dt, h[2]));
    p[0] = ADD(x, p[0]);
    p[1] = ADD(p[1], y);
    p[2] = ADD(p[2], z);
    for (int k = 0; k < 3; k++) v[k] = F32(ADD(v[k], ad[k]));
}

void Integrate_Turn(float out[3], float w[3], const float a[3], float dt)
{
    float ad[3], h[3];
    for (int k = 0; k < 3; k++) ad[k] = F32(MUL(dt, a[k]));
    for (int k = 0; k < 3; k++) h[k] = F32(ADD(MUL(ad[k], 0.5f), w[k]));
    for (int k = 0; k < 3; k++) out[k] = F32(MUL(dt, h[k]));
    for (int k = 0; k < 3; k++) w[k] = F32(ADD(w[k], ad[k]));
}

/* ---- the driving step's pieces (0x473600) ---- */

static uint8_t *wheel_data(uint8_t *d, uint32_t off) { return slot(d, off)->data; }

/* 0x475d00: the terrain's grip under the vehicle (its class's first word, 0x48e330); 0 above 0.2 with
   +0x454 bit 0x400 */
static float terrain_grip(const uint8_t *d)
{
    float g = rdf(g_world.lights + 0x14 * (uint32_t)rd32(d + 0x45c));
    if (!below_eq(g, 0.2f) && (rd32(d + 0x454) & 0x400)) g = 0;   /* 0x4a67a4 */
    return g;
}

/* 0x475e60: the user's steering helped by 0.3 toward a front wheel that is off the ground (its +0x44,
   0x4301e0), within [-1, 1] (the bounds as bits) */
static float steer_assist(uint8_t *d, float s)
{
    if (rd32(wheel_data(d, 0x3ac) + 0x44)) s = F32(ADD(s, 0.3f));   /* 0x4a6808 */
    if (rd32(wheel_data(d, 0x3a8) + 0x44)) s = F32(SUB(s, 0.3f));
    int32_t b;
    memcpy(&b, &s, 4);
    if (b > 0x3f800000) return 1.0f;
    if ((uint32_t)b > 0xbf800000u) return -1.0f;
    return s;
}

void Vehicle_Grip(Node *n, float sp, float *yaw, int32_t *slide, float *grip)
{
    uint8_t *d = n->data, *susp = wheel_data(d, 0x3c8);
    float st = rdf(d + 0xe0);
    /* the four wheels' loads (+0xc) */
    float load = rdf(wheel_data(d, 0x3bc) + 0xc);
    load = F32(ADD(rdf(wheel_data(d, 0x3a8) + 0xc), load));
    load = F32(ADD(rdf(wheel_data(d, 0x3b8) + 0xc), load));
    load = F32(ADD(rdf(wheel_data(d, 0x3ac) + 0xc), load));
    double t = MUL(MUL(MUL(MUL(terrain_grip(d), rdf(d + 0x46c)), rdf(susp + 0x14)), load), 0.25f);   /* 0x4a6748 */
    float lim = F32(t), lim2;
    if (rd32(d + 0x454) & 0x10000) load = lim;
    else load = F32(MUL(load, exe_f32(0x4a67f8)));   /* 7.84 */
    lim2 = F32(MUL(lim, 2.0f));
    if ((n->flags & 0x10) && !g_user_pressed) st = steer_assist(d, st);   /* Node_IsUser, 0x409b70 */
    /* the yaw rate: the steering's, unless the sideways force it needs is over the load */
    if (!below_eq(MUL(MUL(rdf(d + 0x110), sp), sp), load)) st = F32(MUL(DIV(load, sp), st));
    else st = F32(MUL(MUL(rdf(d + 0x110), st), sp));
    double a = fabs(MUL(st, sp));
    float af = F32(a);
    if (!below_eq(a, lim2)) *slide = 1;
    else if (!below_eq(af, lim)) *grip = F32(DIV(SUB(af, lim2), SUB(lim, lim2)));
    *yaw = st;
}

void Vehicle_Force(Node *n, float thr, int upside, const float f[3], uint32_t flags, float *acc, int32_t *slide)
{
    uint8_t *d = n->data;
    int32_t tb;
    memcpy(&tb, &thr, 4);
    if (tb > 0 && (!(flags & 1) || rd32(d + 0x104) == 2)) {   /* no drive with the engine off, or in mode 2 */
        *acc = 0;
        return;
    }
    float dir = rdf(d + 0xe8), sp = rdf(d + 0xac), a = 0;
    uint8_t *brakes = wheel_data(d, 0x3cc), *fl = wheel_data(d, 0x3ac), *fr = wheel_data(d, 0x3a8);
    uint8_t *rl = wheel_data(d, 0x3bc), *rr = wheel_data(d, 0x3b8);
    float k = F32(MUL(rdf(d + 0x46c), 0.25f));
    double vf = ADD(ADD(MUL(rdf(d + 0xc0), f[1]), MUL(rdf(d + 0xc4), f[2])), MUL(rdf(d + 0xbc), f[0]));
    float vff = F32(vf);
    int back = below(vf, 0);
    float lim = F32(MUL(terrain_grip(d), k));
    int locked;
    if (upside) {
        thr = -1.0f;
        locked = 1;
    } else {
        locked = flags & 2;
    }
    memcpy(&tb, &thr, 4);
    int32_t spb;
    memcpy(&spb, &sp, 4);
    if ((uint32_t)tb >= 0xb8d1b717u) {   /* braking: the four wheels' grip */
        lim = F32(-MUL(ADD(ADD(ADD(rdf(rr + 0xc), rdf(rl + 0xc)), rdf(fr + 0xc)), rdf(fl + 0xc)), lim));
        if (spb > 0x3f800000) {
            a = F32(MUL(MUL(rdf(brakes + 0x10), thr), exe_f32(0x4a67f0)));   /* 8 */
            if (locked) {
                double x = MUL(MUL(rdf(d + 0xb0), vff), lim);
                lim = back ? F32(-x) : F32(x);
            }
            if (rd32(d + 0xf0)) lim = F32(MUL(lim, 0.5f));
            if (upside) {
                a = F32(MUL(lim, 2.0f));
            } else if (!below_eq(lim, a)) {
                a = lim;
                *slide = 1;
            }
            if (back) a = F32(-a);
        }
    } else if (tb >= 0x38d1b717) {   /* driving: the rear wheels' grip */
        lim = F32(MUL(ADD(rdf(rr + 0xc), rdf(rl + 0xc)), lim));
        uint8_t *e = wheel_data(d, 0x3c4);
        double t = locked ? MUL(rdf(e + 0x14), rdf(e + 0x18)) : rdf(e + 0x10);   /* the peak or the torque (0x42fad0) */
        a = F32(MUL(t, rdf(d + 0xa8)));
        a = F32(MUL(rdf(d + 0x128), a));
        if (rd32(d + 0x454) & 0x800) a = F32(MUL(a, exe_f32(0x4a67f4)));   /* 5 */
        if (spb > 0x3f800000) a = F32(MUL(rdf(d + 0xb0), a));
        int32_t ab;
        memcpy(&ab, &a, 4);
        if (ab < 0x38d1b717) ab = 0x38d1b717, memcpy(&a, &ab, 4);
        double y = MUL(thr, a);
        a = F32(y);
        if (!below_eq(y, lim)) {
            a = lim;
            *slide = 1;
        }
        a = F32(MUL(dir, a));
    }
    *acc = a;
}

void Frame_Euler(Frame *out, float ax, float ay, float az, float tx, float ty, float tz)
{
    Frame a, b, c, t;
    Frame_Yaw(&a, ay);
    float s = F32(sin(ax)), co = F32(cos(ax));   /* about x */
    memcpy(&b, exe_data(0x4db298, sizeof b), sizeof b);
    b.m[4] = co, b.m[8] = co, b.m[7] = -s, b.m[5] = s;
    Frame_Mul(&t, &b, &a);
    a = t;
    s = F32(sin(az)), co = F32(cos(az));   /* about z */
    memcpy(&c, exe_data(0x4db298, sizeof c), sizeof c);
    c.m[0] = co, c.m[4] = co, c.m[3] = -s, c.m[1] = s;
    Frame_Mul(&t, &c, &a);
    a = t;
    a.pos[0] = tx, a.pos[1] = ty, a.pos[2] = tz;
    *out = a;
}

void Frame_Turn(Frame *out, const Frame *f, float wx, float wy, float wz, float dt)
{
    Frame r, t;
    Frame_Euler(&r, F32(-MUL(dt, wx)), F32(-MUL(dt, wy)), F32(-MUL(dt, wz)), 0, 0, 0);
    Frame_Mul(&t, &r, f);
    *out = t;
}

void Ballistic(float p[3], float v[3], float g, float dt)
{
    p[0] = F32(ADD(MUL(v[0], dt), p[0]));
    p[2] = F32(ADD(MUL(v[2], dt), p[2]));
    double gd = MUL(dt, g);
    p[1] = F32(ADD(MUL(ADD(MUL(0.5f, gd), v[1]), dt), p[1]));   /* 0x4a66b0 */
    v[1] = F32(ADD(gd, v[1]));
}

int Vehicle_Air(Node *n, float dt, uint32_t *flags, float vel[3], float *speed, float w[3], Frame *f)
{
    uint8_t *d = n->data;
    uint32_t fl = (uint32_t)rd32(d + 0x454);
    if (!(fl & 4) && (fl & 0x200)) return 0;
    float p[3] = {F32(n->frame.pos[0]), F32(n->frame.pos[1]), F32(n->frame.pos[2])}, v[3], h;
    memcpy(v, d + 0xbc, sizeof v);
    Ballistic(p, v, -9.8f, dt);   /* 0xc11ccccd */
    Ground_Height(p[0], p[1], p[2], &h, NULL);
    /* the height of the body's bottom over the ground: the node's, or upside down its top's */
    int32_t upb;
    memcpy(&upb, &n->frame.m[4], 4);
    double y = upb > 0 ? (double)p[1] : ADD(MUL(n->frame.m[4], rdf(d + 0x34)), p[1]);
    float above = F32(SUB(y, h));
    int32_t ab;
    memcpy(&ab, &above, 4);
    int air = ((fl & 4) && below(rdf(d + 0x470), above)) || ab > 0x3f000000;   /* rising, or over 0.5 */
    if (!air) {
        putf(d + 0x470, above);
        p[1] = h;
        if (!(fl & 4)) return 0;
    } else {
        putf(d + 0x470, above);
    }
    /* the step's results: the frame at the new place, the velocity, the speed, the turn */
    *f = n->frame;
    for (int k = 0; k < 3; k++) f->pos[k] = p[k];
    memcpy(vel, v, sizeof v);
    *speed = F32(fpu_sqrt(ADD(ADD(MUL(v[1], v[1]), MUL(v[2], v[2])), MUL(v[0], v[0]))));
    memcpy(w, d + 0xc8, 12);
    if (!air) {   /* landing: sliding if it comes down sideways */
        *flags = (fl & ~4u) | 0x200;
        float side = F32(ADD(ADD(MUL(v[2], f->m[2]), MUL(v[1], f->m[1])), MUL(f->m[0], v[0])));
        int32_t sb;
        memcpy(&sb, &side, 4);
        if ((n->flags & 0x10) && ((uint32_t)sb > 0xbf800000u || sb > 0x3f800000)) *flags |= 2;
        return 1;
    }
    /* in the air the nose eases down toward -pi/20 */
    int32_t wb, db;
    memcpy(&wb, &w[0], 4);
    memcpy(&db, &dt, 4);
    if ((uint32_t)wb < 0xbe20d97cu) {
        if ((uint32_t)db > 0x80000000u || db > 0x3f733333) {
            wb = (int32_t)0xbe20d97c, memcpy(&w[0], &wb, 4);
        } else {
            double g = SUB(exe_f32(0x4a67ec), w[0]);
            if (below(fabs(g), 0.005)) wb = (int32_t)0xbe20d97c, memcpy(&w[0], &wb, 4);   /* 0x4a67d8 */
            else w[0] = F32(ADD(MUL(F32(g), dt), w[0]));
        }
    }
    *flags = ((fl | 4) & ~0x2202u);
    Frame t;
    Frame_Turn(&t, f, w[0], w[1], w[2], dt);
    *f = t;
    return 1;
}

void Vec_Split(const float g[3], const float nv[3], float p[3], float t[3])
{
    double dot = ADD(ADD(MUL(nv[1], g[1]), MUL(nv[2], g[2])), MUL(nv[0], g[0]));
    double len2 = ADD(ADD(MUL(nv[1], nv[1]), MUL(nv[2], nv[2])), MUL(nv[0], nv[0]));
    double s = DIV(dot, len2);
    float q[3] = {F32(MUL(s, nv[0])), F32(MUL(nv[1], s)), F32(MUL(s, nv[2]))};
    for (int k = 0; k < 3; k++) t[k] = F32(SUB(g[k], q[k]));
    memcpy(p, q, sizeof q);
}

void Mat_Rows(float out[3], const float v[3], const float *m)
{
    float o[3];
    o[0] = F32(ADD(ADD(MUL(m[1], v[1]), MUL(m[2], v[2])), MUL(m[0], v[0])));
    o[1] = F32(ADD(ADD(MUL(m[3], v[0]), MUL(m[5], v[2])), MUL(m[4], v[1])));
    o[2] = F32(ADD(ADD(MUL(m[7], v[1]), MUL(m[8], v[2])), MUL(m[6], v[0])));
    memcpy(out, o, sizeof o);
}

/* 0x4754a0: friction against sliding sideways, acc += right * -(v . right) / speed * lim */
static void side_friction(Node *n, float lim, float acc[3])
{
    uint8_t *d = n->data;
    const float *r = n->frame.m;
    double m = -MUL(MUL(ADD(ADD(MUL(rdf(d + 0xc0), r[1]), MUL(rdf(d + 0xc4), r[2])), MUL(rdf(d + 0xbc), r[0])),
                        rdf(d + 0xb0)), lim);
    float y = F32(MUL(r[1], m)), z = F32(MUL(m, r[2]));
    acc[0] = F32(ADD(MUL(r[0], m), acc[0]));
    acc[1] = F32(ADD(acc[1], y));
    acc[2] = F32(ADD(acc[2], z));
}

/* 0x475570: a wheel's friction where it touches: its contact point's velocity against the ground, all of
   it when braking or sliding (br), else its sideways part; the force's part along the wheel's arm into
   acc, its torque's yaw into turn */
static void wheel_friction(Node *n, Node *w, float k, int br, float acc[3], float turn[3])
{
    uint8_t *d = n->data, *wd = w->data;
    Frame W;
    Node_FrameIn(&W, w, NULL);
    const float *m = W.m;
    float rx = F32(w->frame.pos[0]);
    double pz = w->frame.pos[2];
    float rz = F32(pz);
    float u[3];
    u[0] = F32(-MUL(pz, rdf(d + 0xcc)));
    u[1] = F32(SUB(MUL(rdf(d + 0xc8), rz), MUL(rdf(d + 0xd0), rx)));
    double uz = MUL(rdf(d + 0xcc), rx);
    u[2] = F32(uz);
    float c[3];
    c[0] = F32(ADD(ADD(ADD(MUL(uz, m[6]), MUL(m[3], u[1])), MUL(m[0], u[0])), rdf(d + 0xbc)));
    c[1] = F32(ADD(ADD(ADD(MUL(m[7], u[2]), MUL(m[4], u[1])), MUL(m[1], u[0])), rdf(d + 0xc0)));
    c[2] = F32(ADD(ADD(ADD(MUL(m[5], u[1]), MUL(m[8], u[2])), MUL(m[2], u[0])), rdf(d + 0xc4)));
    double cx2 = MUL(c[0], c[0]);
    float cx2f = F32(cx2);
    const float eps = exe_f32(0x4a6800);   /* 1e-8 */
    if (below(cx2, eps) && below(MUL(c[1], c[1]), eps) && below(MUL(c[2], c[2]), eps)) return;
    double len = fpu_sqrt(ADD(ADD(MUL(c[1], c[1]), MUL(c[2], c[2])), cx2f));
    float f[3];
    if (br) {
        double q = DIV(-MUL(rdf(wd + 0xc), k), len);
        f[0] = F32(MUL(c[0], q)), f[1] = F32(MUL(c[1], q)), f[2] = F32(MUL(q, c[2]));
    } else {
        double s = ADD(ADD(MUL(c[2], m[2]), MUL(c[1], m[1])), MUL(c[0], m[0]));
        double q = -MUL(MUL(DIV(s, len), rdf(wd + 0xc)), k);
        f[0] = F32(MUL(m[0], q)), f[1] = F32(MUL(m[1], q)), f[2] = F32(MUL(q, m[2]));
    }
    float arm[3] = {F32(ADD(MUL(m[6], rz), MUL(m[0], rx))), F32(ADD(MUL(m[7], rz), MUL(m[1], rx))),
                    F32(ADD(MUL(m[8], rz), MUL(m[2], rx)))};
    float p[3], t[3];
    Vec_Split(f, arm, p, t);
    for (int i = 0; i < 3; i++) acc[i] = F32(ADD(p[i], acc[i]));
    float tq[3] = {F32(SUB(MUL(f[1], arm[2]), MUL(f[2], arm[1]))), F32(SUB(MUL(f[2], arm[0]), MUL(f[0], arm[2]))),
                   F32(SUB(MUL(f[0], arm[1]), MUL(f[1], arm[0])))};
    float r[3];
    Mat_Rows(r, tq, m);
    turn[1] = F32(ADD(turn[1], r[1]));
    turn[2] = 0;
    turn[0] = 0;
}

void Vehicle_Drag(Node *n, int upside, int sliding, int32_t *slide, float drag[3], float slope[3], float turn[3])
{
    uint8_t *d = n->data;
    memset(drag, 0, 12);
    memset(turn, 0, 12);
    float k = F32(MUL(rdf(d + 0x46c), 0.25f)), sp = rdf(d + 0xac);
    int32_t spb;
    memcpy(&spb, &sp, 4);
    int tc = rd32(d + 0x45c);
    static const uint32_t wh[4] = {0x3bc, 0x3a8, 0x3ac, 0x3b8};
    if (spb >= 0x3f800000) {   /* the air's drag and the rolling resistance, against the velocity */
        float air = F32(MUL(MUL(MUL(rdf(d + 0x120), sp), sp), exe_f32(0x4a67fc)));   /* -0.1 */
        float roll = rdf(g_world.lights + 0x14 * (uint32_t)tc + 4);   /* 0x48e340 */
        float s = rdf(wheel_data(d, wh[0]) + 0x14);
        s = F32(ADD(rdf(wheel_data(d, wh[1]) + 0x14), s));
        s = F32(ADD(rdf(wheel_data(d, wh[2]) + 0x14), s));
        double sd = ADD(rdf(wheel_data(d, wh[3]) + 0x14), s);
        double q = MUL(SUB(air, MUL(MUL(sd, roll), k)), rdf(d + 0xb0));
        drag[0] = F32(MUL(rdf(d + 0xbc), q));
        drag[1] = F32(MUL(rdf(d + 0xc0), q));
        drag[2] = F32(MUL(q, rdf(d + 0xc4)));
    }
    /* gravity's parts across and along the ground */
    float g[3], nv[3], pn[3], tg[3];
    memcpy(g, exe_data(0x4a6690, 12), 12);   /* (0, -9.8, 0) */
    memcpy(nv, d + 0x460, 12);
    Vec_Split(g, nv, pn, tg);
    k = F32(MUL(terrain_grip(d), k));
    float s = rdf(wheel_data(d, wh[0]) + 0xc);
    s = F32(ADD(rdf(wheel_data(d, wh[1]) + 0xc), s));
    s = F32(ADD(rdf(wheel_data(d, wh[2]) + 0xc), s));
    double sd = ADD(rdf(wheel_data(d, wh[3]) + 0xc), s);
    float lim = F32(MUL(sd, k));
    int sl;
    if (below(MUL(lim, lim), ADD(ADD(MUL(tg[2], tg[2]), MUL(tg[1], tg[1])), MUL(tg[0], tg[0])))) {
        *slide = 1;
        sl = 1;
    } else {
        sl = sliding;
    }
    if (!upside && !(sl && spb >= 0x3f800000)) {   /* held: only the slope along the forward axis */
        const float *f = n->frame.m + 6;
        double dot = ADD(ADD(MUL(f[2], tg[2]), MUL(tg[1], f[1])), MUL(tg[0], f[0]));
        slope[0] = F32(MUL(f[0], dot));
        slope[1] = F32(MUL(f[1], dot));
        slope[2] = F32(MUL(dot, f[2]));
        return;
    }
    memcpy(slope, tg, 12);
    int32_t tb;
    memcpy(&tb, d + 0xe4, 4);
    int br = upside || (uint32_t)tb > 0xbf000000u || rd32(d + 0xf0);
    if (rd32(d + 0x454) & 0x1000) {
        side_friction(n, lim, drag);
        return;
    }
    static const uint32_t order[4] = {0x3ac, 0x3a8, 0x3bc, 0x3b8};
    for (int i = 0; i < 4; i++) wheel_friction(n, slot(d, order[i]), k, br, drag, turn);
}

/* the velocity reversed against v0 by the step: the step undone (0x4a675c) */
static int reversed(const float v[3], const float v0[3])
{
    return below(ADD(ADD(MUL(v[1], v0[1]), MUL(v[2], v0[2])), MUL(v[0], v0[0])), 0);
}

/* p += a dt^2 / 2 (p doubles) */
static void drift(double p[3], const float a[3], float dt)
{
    double dd = MUL(dt, dt);
    p[0] = ADD(MUL(MUL(dd, a[0]), 0.5), p[0]);   /* 0x4a67c8 */
    p[1] = ADD(MUL(MUL(dd, a[1]), 0.5), p[1]);
    p[2] = ADD(MUL(MUL(dd, a[2]), 0.5), p[2]);
}

int Vehicle_Drive(Node *n, uint32_t *flags, float vel[3], float *speed, float acc_out[3], float w_out[3], Frame *f_out,
                  float dt)
{
    uint8_t *d = n->data;
    const Frame *fr = &n->frame;
    float thr = rdf(d + 0xe4);
    memset(acc_out, 0, 12);
    if (Vehicle_Air(n, dt, flags, vel, speed, w_out, f_out)) return 1;
    uint32_t fl = (uint32_t)rd32(d + 0x454);
    *flags = fl;
    float up[3] = {fr->m[3], fr->m[4], fr->m[5]}, fw[3] = {fr->m[6], fr->m[7], fr->m[8]};
    float rt[3] = {fr->m[0], fr->m[1], fr->m[2]};
    double p[3] = {fr->pos[0], fr->pos[1], fr->pos[2]}, p0[3] = {p[0], p[1], p[2]};
    int upside = below(ADD(ADD(MUL(rdf(d + 0x460), up[0]), MUL(rdf(d + 0x464), up[1])), MUL(rdf(d + 0x468), up[2])), 0.1f);
    int mode = rd32(d + 0x104);
    if (upside || rd32(d + 0xf0) || mode == 0) thr = -1.0f;
    int32_t tb;
    memcpy(&tb, &thr, 4);
    if ((uint32_t)tb < 0xb8d1b717u && mode == 2) thr = 0, tb = 0;
    if ((uint32_t)tb > 0x80000000u && rd32(d + 0xf4)) thr = F32(MUL(thr, 2.0f)), memcpy(&tb, &thr, 4);
    float v0[3];
    memcpy(v0, d + 0xbc, 12);
    float sp = rdf(d + 0xac);
    if (below(ADD(ADD(MUL(fw[1], v0[1]), MUL(fw[2], v0[2])), MUL(fw[0], v0[0])), 0)) sp = -sp;   /* going backward */
    float spin = 1.0f;   /* L40 */
    int32_t sliding, slid;   /* L30, L38 */
    int32_t acb;
    memcpy(&acb, d + 0xac, 4);
    if (below(MUL(rdf(d + 0xe8), sp), -1.0f) && tb > 0x38d1b717) {   /* driving against its motion */
        sliding = slid = 1;
        *flags = fl | 2;
    } else if (rd32(d + 0xf0) && acb > 0x40000000) {
        sliding = slid = 1;
        if (!(fl & 2)) spin = F32(MUL(sp, 0.1f));
    } else {
        slid = 0;
        sliding = fl & 2;
    }
    float a;   /* L34 */
    Vehicle_Force(n, thr, upside, fw, *flags, &a, &slid);
    if (slid) sliding = 1;
    float grip = 1.0f, yaw = 0;   /* Lbc, L3c */
    *flags &= ~0x2000u;
    if (!sliding && !upside) {
        Vehicle_Grip(n, sp, &yaw, &slid, &grip);
        if (slid) sliding = 1;
    }
    float drag[3], slope[3], turn[3];   /* Lb0, Ld8, L124 */
    Vehicle_Drag(n, upside, sliding, &slid, drag, slope, turn);
    if (slid) sliding = 1;
    float v[3], acc[3], l[3];   /* L20, L4c, L10 */
    if (sliding) {
        *flags |= 2;
        memcpy(v, v0, 12);
        Integrate(p, v, drag, dt);
        if (reversed(v, v0)) {
            memset(v, 0, 12);
            memcpy(p, p0, sizeof p);
        }
        for (int k = 0; k < 3; k++) l[k] = F32(ADD(MUL(fw[k], a), slope[k]));
        drift(p, l, dt);
        for (int k = 0; k < 3; k++) v[k] = F32(ADD(MUL(dt, l[k]), v[k]));
        for (int k = 0; k < 3; k++) acc[k] = F32(ADD(drag[k], l[k]));
        if (fl & 0x1000) {   /* the yaw from the steering, eased toward */
            double t = MUL(MUL(rdf(d + 0xe0), rdf(d + 0xe8)), 3.1415927f);   /* 0x4a67b0 */
            float tf = F32(t), target;
            if (acb < 0x3f800000) tf = F32(MUL(rdf(d + 0xac), tf));
            double r = MUL(rdf(d + 0xb4), dt);
            float rf = F32(r);
            int32_t rb;
            memcpy(&rb, &rf, 4);
            if (below(r, 0) || rb > 0x3f733333) {
                target = tf;
            } else {
                double g = SUB(tf, rdf(d + 0xcc));
                target = below(fabs(g), 0.005) ? tf : F32(ADD(MUL(F32(g), rf), rdf(d + 0xcc)));
            }
            yaw = F32(MUL(target, spin));
        } else {   /* the friction's torque turns it */
            float wv[3], ang[3];
            memcpy(wv, d + 0xc8, 12);
            Integrate_Turn(ang, wv, turn, dt);
            int32_t db;
            memcpy(&db, &dt, 4);
            yaw = db > 0x38d1b717 ? F32(DIV(ang[1], dt)) : F32(MUL(ang[1], exe_f32(0x4a6778)));
        }
    } else {
        float side;   /* L10: the sideways pull that turns it */
        if (memcpy(&acb, &grip, 4), acb < 0x3f800000) {   /* part of the grip lost */
            memcpy(v, v0, 12);
            side = F32(-MUL(MUL(grip, yaw), sp));
            *flags |= 0x2000;
        } else {
            for (int k = 0; k < 3; k++) v[k] = F32(MUL(fw[k], sp));
            side = F32(-MUL(yaw, sp));
        }
        float b[3];
        for (int k = 0; k < 3; k++) b[k] = F32(ADD(MUL(side, rt[k]), drag[k]));
        memcpy(drag, b, 12);
        if ((uint32_t)tb > 0x80000000u)
            for (int k = 0; k < 3; k++) drag[k] = F32(ADD(MUL(fw[k], a), b[k]));
        Integrate(p, v, drag, dt);
        if (reversed(v, v0)) {
            memset(v, 0, 12);
            memcpy(p, p0, sizeof p);
        }
        if (tb > 0)
            for (int k = 0; k < 3; k++) acc[k] = F32(ADD(MUL(fw[k], a), slope[k]));
        else
            memcpy(acc, slope, 12);
        drift(p, acc, dt);
        for (int k = 0; k < 3; k++) v[k] = F32(ADD(MUL(dt, acc[k]), v[k]));
        for (int k = 0; k < 3; k++) acc[k] = F32(ADD(drag[k], acc[k]));
    }
    /* turn about the rear axle (+0x11c ahead of the node) by the yaw for the step */
    float rr = rdf(d + 0x11c);
    for (int k = 0; k < 3; k++) p[k] = ADD(MUL(fw[k], rr), p[k]);
    Frame rot, m;
    Frame_Yaw(&rot, F32(-MUL(dt, yaw)));
    Frame_Mul(&m, &rot, fr);
    for (int k = 0; k < 3; k++) p[k] = ADD(MUL(-rr, m.m[6 + k]), p[k]);
    for (int k = 0; k < 3; k++) m.pos[k] = p[k];
    /* the velocity along the ground: (N x v) x N, and the signed speed */
    const float nx = rdf(d + 0x460), ny = rdf(d + 0x464), nz = rdf(d + 0x468);
    float c[3] = {F32(SUB(MUL(ny, v[2]), MUL(nz, v[1]))), F32(SUB(MUL(nz, v[0]), MUL(nx, v[2]))),
                  F32(SUB(MUL(nx, v[1]), MUL(ny, v[0])))};
    float g[3] = {F32(SUB(MUL(nz, c[1]), MUL(ny, c[2]))), F32(SUB(MUL(nx, c[2]), MUL(nz, c[0]))),
                  F32(SUB(MUL(ny, c[0]), MUL(nx, c[1])))};
    memcpy(v, g, 12);
    float s = F32(fpu_sqrt(ADD(ADD(MUL(g[1], g[1]), MUL(g[2], g[2])), MUL(g[0], g[0]))));
    if (below(ADD(ADD(MUL(g[1], m.m[7]), MUL(g[2], m.m[8])), MUL(g[0], m.m[6])), 0)) s = -s;
    int32_t sb;
    memcpy(&sb, &s, 4);
    if ((uint32_t)tb > 0x80000000u && (uint32_t)sb < 0xbf800000u && sb < 0x3f800000) {   /* braked to a stop */
        s = 0;
        memset(v, 0, 12);
        sliding = 0;
        *flags &= ~0x2002u;
    }
    if (sliding && !slid) {   /* no longer sliding sideways */
        double lat = ADD(ADD(MUL(m.m[1], v[1]), MUL(m.m[2], v[2])), MUL(m.m[0], v[0]));
        float lf = F32(lat);
        int32_t lb;
        memcpy(&lb, &lf, 4);
        if (!below_eq(lat, -3.0f) && lb < 0x40400000) *flags &= ~0x2002u;   /* 0x4a67e0 */
    }
    memcpy(vel, v, 12);
    *speed = s;
    memcpy(acc_out, acc, 12);
    memcpy(w_out, d + 0xc8, 12);
    w_out[1] = yaw;
    *f_out = m;
    return 0;
}

void Point_Velocity(float out[3], const float at[3], Node *n)
{
    float v[3] = {0, 0, 0}, p[3] = {at[0], at[1], at[2]};
    for (; n; n = Node_Parent(n)) {
        uint8_t b[0x3c];
        Body_State(b, n);
        float u[3], w[3], t[3];
        memcpy(u, b + 0x18, 12);
        memcpy(w, b + 0x24, 12);
        t[0] = F32(ADD(SUB(MUL(p[1], w[2]), MUL(p[2], w[1])), v[0]));
        t[1] = F32(ADD(SUB(MUL(p[2], w[0]), MUL(p[0], w[2])), v[1]));
        t[2] = F32(ADD(SUB(MUL(p[0], w[1]), MUL(p[1], w[0])), v[2]));
        const float *m = n->frame.m;
        v[0] = F32(ADD(ADD(ADD(MUL(m[3], t[1]), MUL(m[6], t[2])), MUL(m[0], t[0])), u[0]));
        v[1] = F32(ADD(ADD(ADD(MUL(m[4], t[1]), MUL(m[1], t[0])), MUL(m[7], t[2])), u[1]));
        v[2] = F32(ADD(ADD(ADD(MUL(m[8], t[2]), MUL(m[5], t[1])), MUL(m[2], t[0])), u[2]));
        Frame_Points(&p, (const float (*)[3])&p, 1, &n->frame);
    }
    memcpy(out, v, sizeof v);
}

static uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

/* the contact of corner i at the sub-step it: the corner (local), the way back (its velocity reversed) and
   the ground's normal at p */
static int corner_contact(uint8_t *c, const uint8_t *d, int i, int it, float step, const float v[3], const float p[3])
{
    put32(c, 1);
    put32(c + 4, 0);
    Body_Init(c + 8);
    putf(c + 0x44, F32(MUL((double)it, step)));
    memcpy(c + 0x48, d + 4 + 12 * i, 12);
    for (int k = 0; k < 3; k++) putf(c + 0x54 + 4 * k, -v[k]);
    float nv[3];
    Terrain_Normal(nv, p[0], p[2]);
    memcpy(c + 0x60, nv, 12);
    return 1;
}

int Corners_Hit(Node *n, uint8_t *c, float dt)
{
    uint8_t *d = n->data;
    float local[4][3], at[4][3], prev[4][3], v[4][3];
    int32_t done[4] = {0, 0, 0, 0};
    memcpy(local, d + 4, sizeof local);
    Frame_Points(at, (const float (*)[3])local, 4, &n->frame);
    for (int i = 0; i < 4; i++) {
        memcpy(prev[i], at[i], 12);
        Point_Velocity(v[i], local[i], n);
    }
    /* the sub-steps: the speed's metres this step plus one (at most 7), each dt / that (0x4a6864: 1 / n),
       and one more than that many; a reverse speed under -1 m a step has none */
    int32_t k = (int32_t)(int64_t)MUL(rdf(d + 0xac), dt) + 1;
    if (k > 7) k = 7;
    if (k + 1 <= 0) return 0;
    float step = F32(MUL(exe_f32(0x4a6864 + 4 * (uint32_t)k), dt));   /* k 0: -1e-4 */
    for (int it = 0; it <= k; it++)
        for (int i = 0; i < 4; i++) {
            if (done[i]) continue;
            double h = SUB(Terrain_Height(at[i][0], at[i][2]), at[i][1]);
            float hf = F32(h);
            if (below(h, 0.0)) {   /* 0x4a6884: above the ground, on along the velocity */
                memcpy(prev[i], at[i], 12);
                for (int j = 0; j < 3; j++) at[i][j] = F32(ADD(MUL(v[i][j], step), at[i][j]));
                continue;
            }
            float in[3], out[3];   /* in the ground, and out of it */
            memcpy(in, at[i], 12);
            if (it == 0) {
                /* in the ground from the start: back along the velocity until out of it (50 steps at most) */
                float q[3], e = hf;
                memcpy(q, at[i], 12);
                for (int s = 0; fbits(e) <= 0x80000000u && !below(hf, e);) {
                    s++;
                    double z = SUB(q[2], MUL(v[i][2], step));
                    q[0] = F32(SUB(q[0], MUL(v[i][0], step)));
                    q[1] = F32(SUB(q[1], MUL(v[i][1], step)));
                    q[2] = F32(z);
                    e = F32(SUB(Terrain_Height(q[0], z), q[1]));
                    if (s >= 50) break;
                }
                if ((int32_t)fbits(e) > 0) {   /* still in: this corner no more */
                    done[i] = 1;
                    continue;
                }
                if (fbits(e) <= 0xbc23d70au) return corner_contact(c, d, i, it, step, v[i], q);   /* within 0.01 */
                memcpy(out, q, 12);
            } else {
                memcpy(out, prev[i], 12);
            }
            /* halved between the two until within 0.01 above the ground (50 times at most) */
            float mid[3];
            for (int s = 0; s < 50; s++) {
                double z = ADD(MUL(in[2], 0.5f), MUL(out[2], 0.5f));   /* 0x4a6890 */
                mid[0] = F32(ADD(MUL(in[0], 0.5f), MUL(out[0], 0.5f)));
                mid[1] = F32(ADD(MUL(in[1], 0.5f), MUL(out[1], 0.5f)));
                mid[2] = F32(z);
                double g = SUB(Terrain_Height(mid[0], z), mid[1]);
                float gf = F32(g);
                if (below(g, 0.0) && fbits(gf) < 0xbc23d70au) break;
                memcpy((int32_t)fbits(gf) > 0 ? in : out, mid, 12);
            }
            return corner_contact(c, d, i, it, step, v[i], mid);
        }
    return 0;
}

/* 0x45d7c0: the damage of an impact (the node, 0, the velocity reversed, the normal, the corner): not
   ported yet */
static void impact_damage(Node *n, int a, const uint8_t *v, const float nv[3], const uint8_t *at)
{
    (void)n, (void)a, (void)v, (void)nv, (void)at;
}
/* 0x468580: a sound by name at a node: no sound yet */
static void sound_named(const char *wav, Node *n, int a) { (void)wav, (void)n, (void)a; }

static float from_bits(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

/* the turn of a push q at the corner (cx, cz on the ground's plane), q through the frame's rows */
static void push_turn(float out[3], const float *m, const float q[3], float cx, float cz)
{
    double a0 = ADD(ADD(MUL(m[2], q[2]), MUL(m[1], q[1])), MUL(m[0], q[0]));
    float a1 = F32(ADD(ADD(MUL(m[3], q[0]), MUL(m[4], q[1])), MUL(m[5], q[2])));
    double a2 = ADD(ADD(MUL(m[6], q[0]), MUL(m[8], q[2])), MUL(m[7], q[1]));
    out[0] = F32(MUL(a1, cz));
    out[1] = F32(SUB(MUL(a2, cx), MUL(a0, cz)));
    out[2] = F32(-MUL(a1, cx));
}

/* the angular velocity a held within limits on each axis in turn (x pi/2, y pi, z pi/4): over one, the
   whole vector scaled so that axis is at it */
static void turn_limit(float a[3], int k, uint32_t lim)
{
    uint32_t b = fbits(a[k]);
    float l;
    if ((int32_t)b > (int32_t)lim) l = from_bits(lim);
    else if (b > (lim | 0x80000000u)) l = from_bits(lim | 0x80000000u);
    else return;
    float r[3], x = a[k];
    for (int j = 0; j < 3; j++) r[j] = j == k && k == 0 ? l : F32(MUL(DIV(a[j], x), l));
    memcpy(a, r, sizeof r);
}

int Vehicle_Collide(Node *n, float dt)
{
    uint8_t *d = n->data, *c = d + 0x38;
    Node **other = (Node **)Data_Ptr(d, 0x3c);
    if (rd32(c) == 0) {
        if (!Corners_Hit(n, c, dt)) {
            put32(d + 0x454, rd32(d + 0x454) & ~0x10);
            return 0;
        }
        if (rd32(d + 0x454) & 0x10) return 0;   /* still on the same contact */
        put32(d + 0x454, rd32(d + 0x454) | 0x10);
    }
    float v[3], nv[3], cl[3], vn[3], w[3] = {0, 0, 0};
    memcpy(v, d + 0xbc, 12);
    memcpy(nv, c + 0x60, 12);
    memcpy(cl, c + 0x48, 12);
    /* the bounce: 1 - 0.75 (n . v / |v|)^2, within [0.25, 1] (the bits compared as integers) */
    float e = 1.0f;
    if (rd32(d + 0xac) > 0x38d1b717) {   /* 1e-4 */
        double k = MUL(ADD(ADD(MUL(nv[2], v[2]), MUL(nv[1], v[1])), MUL(nv[0], v[0])), rdf(d + 0xb0));
        e = F32(ADD(MUL(MUL(k, k), -0.75f), 1.0f));   /* 0x4a6794, 0x4a6750 */
    }
    if ((int32_t)fbits(e) > 0x3f800000) e = 1.0f;
    else if ((int32_t)fbits(e) < 0x3e800000) e = 0.25f;
    Node *o = *other;
    int mover = o && (o->cls == 1 || o->cls == 7 || o->cls == 9) && o->cls != 7;   /* 0x42e640 */
    if (mover && (n->flags & 0x20)) {
        memcpy(vn, v, sizeof vn);
    } else {
        if (mover) {
            /* against another body: the velocity about the pair's centre of mass, its part along the normal
               reversed, half the rest kept; at least 4 m/s off along the normal */
            float m1 = rdf(d + 0xa4), m2 = rdf(c + 8), u[3], p[3], r[3], t[3];
            memcpy(u, c + 0x20, 12);
            double s[3];
            for (int k = 0; k < 3; k++) s[k] = F32(ADD(MUL(u[k], m2), MUL(v[k], m1)));
            double m = ADD(m2, m1);
            for (int k = 0; k < 3; k++) p[k] = F32(DIV(s[k], m));
            r[0] = F32(SUB(v[0], p[0]));
            r[1] = F32(SUB(v[1], p[1]));
            double rz = SUB(v[2], p[2]);
            r[2] = F32(rz);
            double j = -ADD(ADD(MUL(rz, nv[2]), MUL(r[1], nv[1])), MUL(r[0], nv[0]));
            for (int k = 0; k < 3; k++) t[k] = F32(ADD(MUL(nv[k], j), MUL(r[k], 0.5f)));   /* 0x4a6758 */
            if (below(ADD(ADD(MUL(t[1], t[1]), MUL(t[2], t[2])), MUL(t[0], t[0])), 16.0f))   /* 0x4a6798 */
                for (int k = 0; k < 3; k++) t[k] = F32(MUL(nv[k], 4.0f));                     /* 0x4a679c */
            for (int k = 0; k < 3; k++) vn[k] = F32(ADD(p[k], t[k]));
            float q[3];
            memcpy(q, c + 0x54, 12);
            push_turn(w, n->frame.m, q, cl[0], cl[2]);
            double inv = DIV(1.0f, ADD(MUL(cl[2], cl[2]), MUL(cl[0], cl[0])));
            for (int k = 0; k < 3; k++) w[k] = F32(MUL(w[k], inv));
        } else {
            /* against the ground: the part along the normal reversed, all of it scaled by the bounce */
            double j = MUL(ADD(ADD(MUL(nv[2], v[2]), MUL(nv[1], v[1])), MUL(nv[0], v[0])), -2.0f);   /* 0x4a67a0 */
            for (int k = 0; k < 3; k++) vn[k] = F32(MUL(F32(ADD(MUL(nv[k], j), v[k])), e));
            double q = ADD(ADD(MUL(vn[1], vn[1]), MUL(vn[2], vn[2])), MUL(vn[0], vn[0]));
            if (!below_eq(q, 16.0f)) {   /* a hard one */
                if (!o) {
                    sound_named("vtcoll.wav", n, 0);   /* 0x4dd9c8 */
                    impact_damage(n, 0, c + 0x54, nv, c + 0x48);
                }
            } else if (o || (!(rd32(d + 0x454) & 4) && (int32_t)fbits(n->frame.m[4]) > 0)) {
                /* a soft one, on the wheels' side and not in the air: 4 m/s off along the normal */
                for (int k = 0; k < 3; k++) vn[k] = F32(MUL(nv[k], 4.0f));
            }
            if ((int32_t)fbits(vn[1]) > 0x3f800000) vn[1] = F32(MUL(vn[1], 0.2f));   /* 0x4a67a4 */
            push_turn(w, n->frame.m, nv, cl[0], cl[2]);
            double g = MUL(rdf(d + 0xac), 0.1f);   /* 0x4a6754 */
            for (int k = 0; k < 3; k++) w[k] = F32(MUL(w[k], g));
        }
    }
    if (!(d[0x455] & 2)) {
        float a[3], b = rdf(d + 0xb4);
        for (int k = 0; k < 3; k++) a[k] = F32(ADD(MUL(w[k], b), rdf(d + 0xc8 + 4 * k)));
        turn_limit(a, 0, 0x3fc90fdb);
        turn_limit(a, 1, 0x40490fdb);
        turn_limit(a, 2, 0x3f490fdb);
        memcpy(d + 0xc8, a, 12);
    }
    memcpy(d + 0xbc, vn, 12);
    float sp = F32(fpu_sqrt(ADD(ADD(MUL(vn[1], vn[1]), MUL(vn[2], vn[2])), MUL(vn[0], vn[0]))));
    putf(d + 0xac, sp);
    if ((int32_t)fbits(sp) > 0x38d1b717) putf(d + 0xb0, F32(DIV(1.0f, sp)));
    else put32(d + 0xb0, 0x7149f2ca);
    /* 0x42fea0: the wheels' speed (+0x20); the original doesn't check the four main ones */
    static const uint32_t wheels[6] = {0x3ac, 0x3a8, 0x3bc, 0x3b8, 0x3b4, 0x3b0};
    for (int i = 0; i < 6; i++) {
        Node *wn = slot(d, wheels[i]);
        if (wn) putf(wn->data + 0x20, sp);
    }
    float np[3];
    for (int k = 0; k < 3; k++) np[k] = F32(ADD(MUL(dt, vn[k]), n->frame.pos[k]));
    /* fallen more than 35 m (0x4a67c0) since leaving the ground (+0x474) */
    if (!(n->flags & 0x20) && (rd32(d + 0x454) & 4) && !below_eq(SUB(rdf(d + 0x474), np[1]), 35.0f)) Vehicle_Kill(n);
    for (int k = 0; k < 3; k++) n->frame.pos[k] = np[k];
    Frame t;
    Frame_Turn(&t, &n->frame, rdf(d + 0xc8), rdf(d + 0xcc), rdf(d + 0xd0), dt);
    n->frame = t;
    Motion_Init(c);
    *other = NULL;
    Vehicle_Brake(n, rd32(d + 0x454), 1);
    return 1;
}

/* 0x470ca0: the acceleration's parts along f and across it (along r), as the wheels' load shifts forward
   (o1) and sideways (o2) */
static void load_shift(const float a[3], const float f[3], const float r[3], float *o1, float *o2)
{
    float p[3], t[3];
    Vec_Split(a, f, p, t);
    *o1 = F32(SUB(SUB(MUL(MUL(p[1], f[1]), -0.008f), MUL(MUL(p[2], f[2]), 0.008f)), MUL(MUL(p[0], f[0]), 0.008f)));
    *o2 = F32(SUB(SUB(MUL(MUL(t[1], r[1]), -0.008f), MUL(MUL(t[2], r[2]), 0.008f)), MUL(MUL(t[0], r[0]), 0.008f)));
}

static float wheel_floor(Node *w) { return rdf(w->data + 0x2c); }   /* 0x430120: the wheel's ground */

void Vehicle_Settle(Node *n, const float acc[3], uint32_t mode, int keep, float dt)
{
    uint8_t *d = n->data;
    Node *w[4] = {slot(d, 0x3ac), slot(d, 0x3a8), slot(d, 0x3bc), slot(d, 0x3b8)};
    Node *w4 = slot(d, 0x3b4), *w5 = slot(d, 0x3b0);
    const float *m = n->frame.m;
    float fwd[3] = {m[6], m[7], m[8]}, right[3] = {m[0], m[1], m[2]}, f0[3] = {m[6], m[7], m[8]};
    int32_t tc = rd32(d + 0x45c);
    /* the springs, loaded by the acceleration: front left (o1 - o2), front right, rear left, rear right */
    float o1, o2;
    load_shift(acc, fwd, right, &o1, &o2);
    Wheel_Contact(w[0], 0, F32(SUB(o1, o2)), tc);
    Wheel_Contact(w[1], 0, F32(ADD(o2, o1)), tc);
    Wheel_Contact(w[2], 0, F32(-ADD(o2, o1)), tc);
    Wheel_Contact(w[3], 0, F32(SUB(o2, o1)), tc);
    if (w4) Wheel_Contact(w4, 0, 0, tc);
    if (w5) Wheel_Contact(w5, 0, 0, tc);
    /* the heights to hold above the ground: the wheels' bottoms when upright enough (the up axis's y over
       +0x478), else the box's bottom (+0x34) under its corners (the fourth corner at the first wheel, the
       third at the second, the first at the third, the second at the fourth) */
    int up = !below_eq(m[4], rdf(d + 0x478));
    float y[4];
    if (up) {
        for (int i = 0; i < 4; i++) {
            float p[3];
            Node_WorldPos(p, w[i]);   /* 0x430140: the wheel's bottom, less its radius (+0x1c) */
            y[i] = F32(SUB(p[1], rdf(w[i]->data + 0x1c)));
        }
    } else {
        static const int at[4] = {2, 3, 1, 0};
        for (int i = 0; i < 4; i++) {
            float p[1][3];
            memcpy(p[0], d + 4 + 12 * i, 12);
            memcpy(&p[0][1], d + 0x34, 4);
            Frame_Points(p, (const float (*)[3])p, 1, &n->frame);
            y[at[i]] = p[0][1];
        }
    }
    /* the deepest into the ground */
    float g = F32(SUB(wheel_floor(w[0]), y[0]));
    for (int i = 1; i < 4; i++) {
        double t = SUB(wheel_floor(w[i]), y[i]);
        if (!below_eq(t, g)) g = F32(t);
    }
    /* the wheels off the ground (more than 1e-4 above it once the deepest is out): a turn to bring each
       down (bits 8, 4, 2, 1 of lift) */
    const float pi = from_bits(0x40490fdb), pi2 = from_bits(0x3fc90fdb);   /* 0x4a6634, 0x4a6630 */
    float r[3] = {0, 0, 0};
    int32_t lift = 0;
    if (!below_eq(ADD(SUB(y[0], wheel_floor(w[0])), g), 1e-4f)) {   /* 0x4a6628 */
        r[2] = F32(SUB(r[2], pi2));
        lift = 8;
        r[0] = from_bits(0xc0490fdb);
    }
    if (!below_eq(ADD(SUB(y[1], wheel_floor(w[1])), g), 1e-4f)) {
        r[0] = F32(SUB(r[0], pi));
        lift |= 4;
        r[2] = F32(ADD(r[2], pi2));
    }
    if (!below_eq(ADD(SUB(g, wheel_floor(w[2])), y[2]), 1e-4f)) {
        r[0] = F32(ADD(r[0], pi));
        lift |= 2;
        r[2] = F32(SUB(r[2], pi2));
    }
    if (!below_eq(ADD(SUB(g, wheel_floor(w[3])), y[3]), 1e-4f)) {
        r[0] = F32(ADD(r[0], pi));
        lift |= 1;
        r[2] = F32(ADD(r[2], pi2));
    }
    if (!up) {
        r[0] = -r[0];
        r[2] = -r[2];
    }
    /* eased from the angular velocity's x and z by 2 dt (when 2 dt is in [0, 0.95]) unless within 0.005 */
    double su = MUL(dt, 2.0f);   /* 0x4a6638 */
    float sf = F32(su);
    if (!below(su, 0.0f) && (int32_t)fbits(sf) <= 0x3f733333) {
        double e = SUB(r[0], rdf(d + 0xc8));
        float ef = F32(e);
        if (!below(fabs(e), 0.005)) r[0] = F32(ADD(MUL(MUL(dt, ef), 2.0f), rdf(d + 0xc8)));   /* 0x4a6640 */
    }
    if (fbits(sf) <= 0x80000000u && (int32_t)fbits(sf) <= 0x3f733333) {
        double e = SUB(r[2], rdf(d + 0xd0));
        float ef = F32(e);
        if (!below(fabs(e), 0.005)) r[2] = F32(ADD(MUL(MUL(dt, ef), 2.0f), rdf(d + 0xd0)));
    }
    /* out of the ground along its normal */
    float nv[3];
    memcpy(nv, d + 0x460, 12);
    for (int k = 0; k < 3; k++) n->frame.pos[k] = ADD(MUL(nv[k], g), n->frame.pos[k]);
    /* turned about the pivot for the lifted wheels (+0x488, a local x and z for each lift) */
    Frame fr;
    float tiny = exe_f32(0x4a6648);   /* 1e-8 */
    if (below(MUL(r[0], r[0]), tiny) && below(MUL(r[1], r[1]), tiny) && below(MUL(r[2], r[2]), tiny)) {
        fr = n->frame;
    } else {
        float a = rdf(d + 0x488 + 8 * (uint32_t)lift), b = rdf(d + 0x48c + 8 * (uint32_t)lift);
        double s0 = ADD(n->frame.pos[0], ADD(MUL(m[6], b), MUL(m[0], a)));
        double s1 = ADD(n->frame.pos[1], ADD(MUL(m[7], b), MUL(m[1], a)));
        double s2 = ADD(n->frame.pos[2], ADD(MUL(b, m[8]), MUL(m[2], a)));
        Frame_Turn(&fr, &n->frame, r[0], r[1], r[2], dt);
        double na = -a, nb = -b;
        fr.pos[0] = ADD(s0, ADD(MUL(fr.m[0], na), MUL(fr.m[6], nb)));
        fr.pos[1] = ADD(s1, ADD(MUL(fr.m[1], na), MUL(fr.m[7], nb)));
        fr.pos[2] = ADD(s2, ADD(MUL(na, fr.m[2]), MUL(fr.m[8], nb)));
    }
    /* the plane of the wheels' contacts (the front ones and the rear axle's middle) */
    float wp[4][3], rm[3], pn[3];
    for (int i = 0; i < 4; i++) Node_WorldPos(wp[i], w[i]);
    for (int i = 0; i < 4; i++) wp[i][1] = F32(SUB(wheel_floor(w[i]), rdf(w[i]->data + 0x24)));   /* 0x430130 */
    for (int k = 0; k < 3; k++) rm[k] = F32(MUL(F32(ADD(wp[2][k], wp[3][k])), 0.5f));   /* 0x4a664c */
    Plane_Normal(pn, wp[1], rm, wp[0]);
    if (!keep) {
        /* the turned frame kept unless the plane is nearer the current up than the turned one is (or a
           landing or a slide, mode) */
        double a1 = ADD(ADD(MUL(m[4], fr.m[4]), MUL(m[5], fr.m[5])), MUL(m[3], fr.m[3]));
        double a2 = ADD(ADD(MUL(m[4], pn[1]), MUL(m[5], pn[2])), MUL(m[3], pn[0]));
        if (mode || below_eq(a2, a1)) {
            memcpy(d + 0xc8, &r[0], 4);
            memcpy(d + 0xd0, &r[2], 4);
            n->frame = fr;
            return;
        }
    } else if (fbits(pn[1]) > 0x80000000u) {
        for (int k = 0; k < 3; k++) pn[k] = -pn[k];
    }
    /* the frame on the plane: up its normal, forward the old one levelled onto it */
    if ((int32_t)fbits(pn[1]) > 0x38d1b717) f0[1] = F32(-DIV(ADD(MUL(pn[2], f0[2]), MUL(pn[0], f0[0])), pn[1]));
    double pos[3] = {n->frame.pos[0], n->frame.pos[1], n->frame.pos[2]};
    Frame_UpFwd(&n->frame, pn, f0);
    memcpy(n->frame.pos, pos, sizeof pos);
    put32(d + 0xc8, 0);
    put32(d + 0xd0, 0);
}

/* 0x42a5d0: the wear of rough ground (the terrain class's +0x10 times dt summed at +0x458, a random part
   damaged per whole unit, 0x42bac0, 0x42bf90, 0x42d230): with the damage, not ported yet */
static void terrain_wear(Node *n, float amount) { (void)n, (void)amount; }
/* 0x4683c0: a sound by number at a node: no sound yet */
static void sound_id(int id, Node *n, int a) { (void)id, (void)n, (void)a; }

void Vehicle_Physics(Node *n, float dt)
{
    Vehicle_Collide(n, dt);
    Vehicle_Upright(n);
    uint8_t *d = n->data;
    if (rd32(d + 0x454) & 0x8020) put32(d + 0xe4, 0xbf800000);   /* dying: braking */
    Vehicle_Steer(n);
    static const uint32_t wo[6] = {0x3ac, 0x3a8, 0x3bc, 0x3b8, 0x3b4, 0x3b0};
    Node *w[6];
    for (int i = 0; i < 6; i++) w[i] = slot(d, wo[i]);
    uint32_t flags = 0;
    float speed = 0, acc[3] = {0, 0, 0};
    Vehicle_Drive(n, &flags, (float *)(d + 0xbc), &speed, acc, (float *)(d + 0xc8), &n->frame, dt);   /* in place */
    memcpy(d + 0xd4, acc, 12);
    putf(d + 0xac, fabsf(speed));
    if (rd32(d + 0xac) > 0x38d1b717) putf(d + 0xb0, F32(DIV(1.0f, rdf(d + 0xac))));
    else put32(d + 0xb0, 0x7149f2ca);
    uint8_t *engine = slot(d, 0x3c4)->data;
    int reverse = (uint32_t)rd32(d + 0xe8) > 0x80000000u;
    if (flags & 4) {   /* in the air: the wheels hang */
        Vehicle_Brake(n, rd32(d + 0x454), flags & 2);
        put32(d + 0x454, flags);
        for (int i = 0; i < 6; i++)
            if (i < 4 || w[i]) Wheel_Contact(w[i], 1, 0, 0);
        Engine_Step(engine, rdf(d + 0xe4), rdf(d + 0xac), rd32(d + 0x454), reverse, rd32(d + 0x104));
        return;
    }
    uint32_t old = rd32(d + 0x454);
    if (old & 4) {   /* landed: more than 35 m (0x4a6780) below where it left the ground kills */
        if (!(old & 0x8020) && !below_eq(SUB(rdf(d + 0x474), n->frame.pos[1]), 35.0) && !(n->flags & 0x20)) {
            Vehicle_Kill(n);
        } else {
            static const float at[3] = {0, -0.2f, 0};
            float nv[3], back[3];
            for (int k = 0; k < 3; k++) back[k] = -rdf(d + 0xbc + 4 * k);
            memcpy(nv, d + 0x460, 12);
            sound_id(4, n, 0);
            impact_damage(n, 0, (const uint8_t *)back, nv, (const uint8_t *)at);
        }
    }
    old = rd32(d + 0x454);
    put32(d + 0x454, flags);
    for (int i = 0; i < 6; i++)
        if (w[i]) putf(w[i]->data + 0x20, speed);   /* 0x42fea0 (the four main ones unchecked) */
    /* the ground under the vehicle (or, on its back, under its box's bottom): its class, height and
       normal; +0x46c gravity times the normal's y cubed */
    float p[3];
    for (int k = 0; k < 3; k++) p[k] = F32(n->frame.pos[k]);
    if (fbits(n->frame.m[4]) > 0x80000000u) {
        float b = rdf(d + 0x34), q[3];
        for (int k = 0; k < 3; k++) q[k] = F32(ADD(MUL(n->frame.m[3 + k], b), p[k]));
        memcpy(p, q, sizeof q);
    }
    put32(d + 0x45c, Terrain_Class(p[0], p[2]));
    float h, nv[3];
    Ground_Height(p[0], p[1], p[2], &h, nv);
    memcpy(d + 0x460, nv, 12);
    float ny = rdf(d + 0x464);
    putf(d + 0x46c, F32(MUL(MUL(MUL(ny, ny), ny), 9.8f)));   /* 0x4a678c */
    if (!(rd32(d + 0x454) & 4)) putf(d + 0x474, h);
    Vehicle_Settle(n, acc, old & 0x202, 0, dt);
    Vehicle_Brake(n, old, flags & 2);
    if ((int32_t)fbits(speed) > 0x40f4cccd && !(n->flags & 0x20)) {   /* faster than 7.65 */
        int32_t tc = rd32(d + 0x45c);
        float wear = (float)rd32(g_world.lights + 0x14 * (uint32_t)tc + 0x10);   /* 0x48e360 */
        terrain_wear(n, F32(MUL(wear, dt)));
    }
    Engine_Step(engine, rdf(d + 0xe4), speed, flags, reverse, rd32(d + 0x104));
}
