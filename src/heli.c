/* The helicopters (class 9): their flight and their crash (docs/re/vehicles.md "The helicopters") */
#include "heli.h"
#include "net/netgame.h"
#include "ai.h"
#include "camera.h"
#include "classes.h"
#include "clock.h"
#include "cockpit.h"
#include "collide.h"
#include "crt.h"
#include "damage.h"
#include "effects.h"
#include "entity.h"
#include "exe.h"
#include "input.h"
#include "lod.h"
#include "math3d.h"
#include "melee.h"
#include "physics.h"
#include "sound.h"
#include "tex.h"
#include "weapons.h"
#include "render/fpu.h"
#include <math.h>
#include <string.h>
#include <string.h>

static uint16_t rd16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }

#define ADD fpu_add
#define MUL fpu_mul
#define SUB fpu_sub
#define DIV fpu_div
#define F32 fpu_f32

static void put32(uint8_t *p, int32_t v) { memcpy(p, &v, 4); }
static uint32_t rdu(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static float rdf(const uint8_t *p) { float v; memcpy(&v, p, 4); return v; }
static void putf(uint8_t *p, float v) { memcpy(p, &v, 4); }
static uint32_t fb(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

/* the step dt is usable as a rate when it is not negative (its bits at most -0's) and below 0.95 */
static int rate_ok(float k) { return fb(k) <= 0x80000000u && (int32_t)fb(k) <= 0x3f733333; }

/* cur eased toward to by dt, or set to it when dt is out of range or the gap is under 0.005 (0x4a5d48) */
static void ease(uint8_t *cur, float to, float dt)
{
    if (rate_ok(dt)) {
        double e = SUB(to, rdf(cur));
        if (!(fabs(e) < 0.005)) {
            putf(cur, F32(ADD(MUL(F32(e), dt), rdf(cur))));
            return;
        }
    }
    putf(cur, to);
}

/* 0x431de0: one step of the flight, on the data's +0x454 (flags), +0xbc (velocity), +0xac (speed),
   +0xc8 (the turn) and the node's frame; acc gets the step's acceleration. The 0x508 block's +0xc
   (throttle) and +0x10 (bank) follow the controls +0xe4 and +0xe0, held at 0 below a height of 10; the
   thrust is the throttle x 20.2 along the nose (flattened) and the bank x -20.2 sideways. A computer's
   helicopter with no thrust brakes against its motion instead, stopping when it would reverse. Above a
   speed of 1 the drag is -0.00155 x speed x velocity. Under the ground it dies (0x20); its height eases
   (dt / 2) toward the block's +0x14 over the higher of the ground below and the ground 2 s ahead. The
   frame is rebuilt each step, its up along the acceleration plus 19.6 up, its nose along the velocity
   when that is above 1 and ahead of the nose, else the flattened nose. Falling (0x10) it drops at 9.8,
   lands on the ground (within 0.5) and turns by +0xc8; on the ground it is dead. Parked (0x80) only the
   frame is rebuilt. */
static void heli_fly(Node *n, float acc[3], float dt)
{
    uint8_t *d = n->data;
    uint8_t *b = *Data_Ptr(d, 0x508);
    const float *m = n->frame.m;
    float pos[3], v[3], velo[3];
    for (int i = 0; i < 3; i++) {
        pos[i] = F32(n->frame.pos[i]);
        v[i] = rdf(d + 0xbc + 4 * (uint32_t)i);
        velo[i] = v[i];
    }
    float sp = rdf(d + 0xac);
    uint32_t fl = rdu(d + 0x454);
    double inv = Fast_InvSqrt(ADD(MUL(m[8], m[8]), MUL(m[6], m[6])));
    float hf[3] = {F32(MUL(m[6], inv)), F32(MUL(0.0f, inv)), F32(MUL(inv, m[8]))};   /* the nose, flat */
    inv = Fast_InvSqrt(ADD(MUL(m[2], m[2]), MUL(m[0], m[0])));
    float sx = F32(MUL(m[0], inv)), sz = F32(MUL(inv, m[2]));   /* the side, flat */
    float a[3] = {0, 0, 0};
    Frame f = n->frame;

    if (fl & 0x10) {   /* falling */
        float p[3], g;
        for (int i = 0; i < 3; i++) p[i] = F32(n->frame.pos[i]);
        Ballistic(p, v, -9.8f, dt);
        Ground_Height(p[0], p[1], p[2], &g, NULL);
        if (!(ADD(g, 0.5) < p[1])) {   /* 0x4a5d38 */
            p[1] = g;
            v[1] = 0;
        }
        for (int i = 0; i < 3; i++) f.pos[i] = p[i];   /* 0x432ab0 */
        memcpy(velo, v, sizeof velo);
        float w[3] = {rdf(d + 0xc8), rdf(d + 0xcc), rdf(d + 0xd0)};
        Frame t;
        Frame_Turn(&t, &f, w[0], w[1], w[2], dt);
        f = t;
        for (int i = 0; i < 3; i++) pos[i] = F32(f.pos[i]);
        Ground_Height(pos[0], pos[1], pos[2], &g, NULL);
        if (pos[1] == g) fl |= 0x20;
    } else if (!(fl & 0x80)) {   /* flying */
        if ((int32_t)fb(pos[1]) < 0x41200000) {   /* below 10 by the bits */
            putf(b + 0xc, 0);
            putf(b + 0x10, 0);
        } else {
            ease(b + 0xc, rdf(d + 0xe4), dt);
            ease(b + 0x10, rdf(d + 0xe0), dt);
        }
        float t = F32(MUL(rdf(b + 0xc), 20.2f)), s = F32(MUL(rdf(b + 0x10), -20.2f));   /* 0x4a5d50, 0x4a5d54 */
        if ((int32_t)fb(t) < 1 && !(n->flags & 0x10)) {   /* Node_IsUser 0x42e620 */
            if (-t < sp) {
                a[0] = F32(ADD(MUL(DIV(v[0], sp), t), MUL(sx, s)));
                a[1] = F32(MUL(DIV(v[1], sp), t));
                a[2] = F32(ADD(MUL(DIV(v[2], sp), t), MUL(sz, s)));
            } else if ((int32_t)fb(sp) > 0x3f800000) {
                a[0] = F32(SUB(MUL(sx, s), v[0]));
                a[1] = -v[1];
                a[2] = F32(SUB(MUL(sz, s), v[2]));
            } else if ((int32_t)fb(sp) > 0) {
                for (int i = 0; i < 3; i++) a[i] = -v[i];
            } else {
                sp = 0;
                for (int i = 0; i < 3; i++) v[i] = 0;
            }
        } else {
            a[0] = F32(ADD(MUL(hf[0], t), MUL(sx, s)));
            a[1] = F32(MUL(hf[1], t));
            a[2] = F32(ADD(MUL(hf[2], t), MUL(sz, s)));
        }
        if ((int32_t)fb(sp) > 0x3f800000)
            for (int i = 0; i < 3; i++) a[i] = F32(ADD(MUL(MUL(v[i], sp), -0.00155f), a[i]));   /* 0x4a5d58 */
        memcpy(velo, v, sizeof velo);
        Integrate_F(pos, velo, a, dt);
        if (!(n->flags & 0x10) && fb(rdf(b + 0xc)) > 0x80000000u &&
            ADD(ADD(MUL(velo[2], v[2]), MUL(velo[1], v[1])), MUL(velo[0], v[0])) < 0.0) {   /* it would reverse */
            for (int i = 0; i < 3; i++) velo[i] = a[i] = 0;
        }
        float g1, g2, ahead[3];
        Ground_Height(pos[0], pos[1], pos[2], &g1, NULL);
        if (pos[1] < g1) {
            for (int i = 0; i < 3; i++) velo[i] = a[i] = 0;
            fl |= 0x20;
        }
        for (int i = 0; i < 3; i++) ahead[i] = F32(ADD(MUL(velo[i], 2.0f), pos[i]));   /* 0x4a5d5c */
        Ground_Height(ahead[0], ahead[1], ahead[2], &g2, NULL);
        float to = F32(ADD(rdf(b + 0x14), g2 > g1 ? g2 : g1));
        double k = MUL(dt, 0.5f);
        if (k < 0.0 || (int32_t)fb(F32(k)) > 0x3f733333) pos[1] = to;
        else {
            double e = SUB(to, pos[1]);
            pos[1] = fabs(e) < 0.005 ? to : F32(ADD(MUL(MUL(F32(e), dt), 0.5f), pos[1]));
        }
    }
    if (!(fl & 0x10)) {   /* the frame from the acceleration and the motion */
        putf(d + 0xc8, 0);
        putf(d + 0xcc, 0);
        putf(d + 0xd0, 0);
        float u[3] = {a[0], F32(ADD(a[1], 19.6f)), a[2]};   /* 0x4a5d60 */
        inv = Fast_InvSqrt(ADD(ADD(MUL(u[2], u[2]), MUL(u[1], u[1])), MUL(u[0], u[0])));
        float up[3] = {F32(MUL(u[0], inv)), F32(MUL(u[1], inv)), F32(MUL(inv, u[2]))};
        const float *fwd = hf;
        if ((int32_t)fb(sp) >= 0x3f800000 &&
            ADD(ADD(MUL(hf[1], velo[1]), MUL(hf[2], velo[2])), MUL(hf[0], velo[0])) > 0.0)
            fwd = velo;
        Frame_UpFwd(&f, up, fwd);
    }
    memcpy(acc, a, sizeof a);
    for (int i = 0; i < 3; i++) putf(d + 0xbc + 4 * (uint32_t)i, velo[i]);
    putf(d + 0xac, F32(fpu_sqrt(ADD(ADD(MUL(velo[1], velo[1]), MUL(velo[2], velo[2])), MUL(velo[0], velo[0])))));
    for (int i = 0; i < 3; i++) f.pos[i] = pos[i];
    put32(d + 0x454, (int32_t)fl);
    n->frame = f;
}

/* 0x432920: part i (the data's +0x408 slots, by the table 0x4d5ce8) in a state by level (0-4: 0, 0, 1,
   2, 3), dropping the entity's sphere when it changes */
static void part_state(uint8_t *d, int i, int level)
{
    Node *part = *Data_Ptr(d, 0x408 + 4 * exe_u32(0x4d5ce8 + 4u * (uint32_t)i));
    static const int state[5] = {0, 0, 1, 2, 3};
    if (part && Lod_SetState(part, state[level])) Entity_DropSphere(*Data_Ptr(d, 0));
}
/* 0x4329b0: the part's damage textures (0x4d5d00's count of the 13-byte names at 0x4d5d18, ten a part) at
   a frame by level (0, 1, 1, 2, 3) */
static void part_textures(uint8_t *d, int i, int level)
{
    Node *part = *Data_Ptr(d, 0x408 + 4 * exe_u32(0x4d5ce8 + 4u * (uint32_t)i));
    static const int frame[5] = {0, 1, 1, 2, 3};
    if (!part) return;
    for (int j = 0; j < (int32_t)exe_u32(0x4d5d00 + 4u * (uint32_t)i); j++)
        Tex_SetFrame((uintptr_t)part, exe_str(0x4d5d18 + ((uint32_t)i * 10 + (uint32_t)j) * 13), 0, frame[level], true);
}

static void explode(Node *n, const float at[3], Node *on)
{
    char name[8] = {0};
    strncpy(name, exe_str(0x4d539c), 8);   /* x1_bldx1 */
    Explosion_Spawn((const uint8_t *)name, n, at[0], at[1], at[2], on);   /* 0x4540d0 */
}

/* Heli_Step 0x431840 (network play's, another player's helicopter stepped over dt) and Heli_Frame's body:
   the same code but for the step (the parts' states written out, 0x432920's, and the crash's places read
   from the frame alone) */
void Heli_Step(Node *n, float dt)
{
    uint8_t *d = n->data;
    float at[3];
    /* a hit in the motion block (+0x38) brings it down as the lethal damage does (0x431c60) */
    if (rdu(d + 0x38) && !(rdu(d + 0x454) & 0x30)) {
        put32(*Data_Ptr(d, 0x508), 0);
        put32(d + 0x454, (int32_t)(rdu(d + 0x454) | 0x10));
        put32(d + 0xc8, 0x3e4ccccd);
        put32(d + 0xcc, 0);
        put32(d + 0xd0, (int32_t)0xbf000000u);
        for (int i = 0; i < 3; i++) at[i] = F32(n->frame.pos[i]);
        explode(n, at, NULL);
    }
    if (rdu(d + 0x454) & 0x20) return;
    if ((rdu(d + 0x454) & 0x80) && (int32_t)rdu(d + 0xe4) > 0x38d1b717) {   /* parked, the throttle up */
        putf((uint8_t *)*Data_Ptr(d, 0x508) + 0x14, 20.0f);
        put32(d + 0x454, (int32_t)(rdu(d + 0x454) & ~0x80u));
    }
    float acc[3];
    heli_fly(n, acc, dt);
    if (!(rdu(d + 0x454) & 0x20)) return;
    /* the crash */
    Entity *e = *Data_Ptr(d, 0);
    Entity_DropSphere(e);
    if (g_user_entity && g_user_entity->node == n) {
        g_input_menu = 0;                         /* 0x480680 */
        Camera_SetMode(g_user_entity, 0xfefe);    /* the death camera */
        Dash_Off();                               /* 0x428890 */
        int32_t eight = 8;
        memcpy(INPUT_AT(0x4def9c), &eight, 4);
        if (Net_On()) Net_Died();   /* 0x462d50 */
    }
    for (int i = 0; i < 3; i++) at[i] = F32(n->frame.pos[i]);   /* 0x432a70 */
    explode(n, at, NULL);
    explode(n, at, n);
    for (int i = 0; i < 5; i++) {
        part_state(d, i, 4);
        part_textures(d, i, 4);
    }
    Node_SetFlags(n, 0x201);
    for (Node *c = n->child; c; c = c->next) Chunk_Spawn(c);   /* 0x45b0a0 */
    float still[3] = {0, 0, 0};
    for (int k = 0; k < 10; k++) {   /* CHUNK1 (0x4d5394) on an odd rand, else CHUNK2 (0x4d538c) */
        const uint8_t *name = exe_data(crt_rand() % 2 != 0 ? 0x4d5394 : 0x4d538c, 8);
        if (name) Particle_New(name, at[0], at[1], at[2], still, 15.0f);   /* 0x45c780 */
    }
    Weapons_Unarm(n);          /* 0x455370 */
    AI_Rethink();              /* 0x408bf0 */
    Sound_Event(7, n, NULL);   /* 0x4683c0 */
    /* a computer's in the melee (0x407370), or a gone player's in network play (0x4644f0), is taken out
       of the world */
    if (!(n->flags & 0x10) && (g_melee || (Net_On() && !NetPl_Name(rd16(d + 0x480), NULL)))) {
        Vehicle_TakeOut(e);   /* 0x42b7d0 */
        return;
    }
    Collide_Remove(e);    /* 0x469cd0 */
    Engine_SoundStop(n);  /* 0x4763b0 */
}

/* Heli_Frame 0x4314a0: the step by the frame's (g_clock_dt, 0x450e50) */
void Heli_Frame(Node *n) { Heli_Step(n, g_clock_dt); }
