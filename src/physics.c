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
