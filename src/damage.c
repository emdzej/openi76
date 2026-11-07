/* A vehicle's state from step to step (damage.h). In the current rounding mode (render/fpu.h). */
#include "damage.h"
#include "classes.h"
#include "entity.h"
#include "exe.h"
#include "ai.h"
#include "crt.h"
#include "physics.h"
#include "weapons.h"
#include "render/fpu.h"
#include "render/light.h"
#include "lod.h"
#include "input.h"
#include "math3d.h"
#include "tex.h"
#include <string.h>

static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }
static void put32(uint8_t *p, int32_t v) { memcpy(p, &v, 4); }
static float rdf(const uint8_t *p) { float v; memcpy(&v, p, 4); return v; }
static void putf(uint8_t *p, float v) { memcpy(p, &v, 4); }
static Node *slot(uint8_t *d, uint32_t off) { return *Data_Ptr(d, off); }

/* 0x42e250 and 0x42e290: the node, everything after it and everything under it */
static void flags_set_all(Node *n, uint32_t bits)
{
    if (n->next) flags_set_all(n->next, bits);
    if (n->child) flags_set_all(n->child, bits);
    n->flags |= bits;
}
static void flags_clear_all(Node *n, uint32_t bits)
{
    if (n->next) flags_clear_all(n->next, bits);
    if (n->child) flags_clear_all(n->child, bits);
    n->flags &= ~bits;
}

void Node_SetFlags(Node *n, uint32_t bits)
{
    if (!n) return;
    Node *c = n->child;
    n->flags |= bits;
    if (!c) return;
    if (c->next) flags_set_all(c->next, bits);
    if (c->child) flags_set_all(c->child, bits);
    c->flags |= bits;
}

void Node_ClearFlags(Node *n, uint32_t bits)
{
    if (!n) return;
    Node *c = n->child;
    n->flags &= ~bits;
    if (!c) return;
    if (c->next) flags_clear_all(c->next, bits);
    if (c->child) flags_clear_all(c->child, bits);
    c->flags &= ~bits;
}

/* a part's health as a fraction: cur / max, or cur * 0.01 without a maximum */
static float whole(const uint8_t *d, uint32_t cur, uint32_t max)
{
    int32_t m = rd32(d + max);
    if (m) return fpu_f32(fpu_div((double)rd32(d + cur), (double)m));
    return fpu_f32(fpu_mul((double)rd32(d + cur), exe_f32(0x4a59fc)));
}

/* the lesser of two lamps' (b unless b is above a) below a half */
static int broken(float a, float b)
{
    float m = b > a ? a : b;
    return m < 0.5f || m != m;   /* 0x4a5a04 */
}

void Vehicle_Lights(Node *n, int toggle)
{
    uint8_t *d = n->data;
    if (toggle) put32(d + 0x454, rd32(d + 0x454) ^ 0x40);
    int on = (int)((uint32_t)rd32(d + 0x454) & 0x40) >> 6;
    if (broken(whole(d, 0x178, 0x158), whole(d, 0x18c, 0x168))) on = 0;
    if (toggle) Light_SetOn(n, on != 0);
    Node *beam = slot(d, 0x440);
    if (beam) {
        beam->flags &= ~1u;
        beam->flags |= on == 0;
    }
    if (on) Node_ClearFlags(slot(d, 0x3a0), 1);
    else Node_SetFlags(slot(d, 0x3a0), 1);
    int brake = (int)((uint32_t)rd32(d + 0x454) & 0x40) >> 6;
    if ((uint32_t)rd32(d + 0xe4) > 0xbe4ccccdu) brake = 2;   /* the throttle below -0.2 */
    if (broken(whole(d, 0x184, 0x164), whole(d, 0x198, 0x174))) brake = 0;
    Node *rear = slot(d, 0x3a4);
    if (rear) {
        rear->flags &= ~1u;
        rear->flags |= brake != 2;
    }
}

/* 0x467260: a sound played. The sound system is not ported yet: as if it played (with the disc's
   samples it does) */
static int sound_play(const char *name) { (void)name; return 1; }

float Engine_StartTime(Node *n)
{
    if (!(n->flags & 0x10) || !n->data) return 0;   /* 0x42e620: the user's */
    const uint8_t *e = slot(n->data, 0x3c4)->data;
    if (!e) return 0;
    /* 0x4764b0: the sample by the engine's state, the kind's entry of 0x14 bytes (a name, its length) */
    double r = fpu_div((double)rd32(e), (double)rd32(e + 4));
    float rf = fpu_f32(r);
    uint32_t rb;
    memcpy(&rb, &rf, 4);
    uint32_t table = r > 0.66f ? 0x4ddb40u : (int32_t)rb > 0x3ea8f5c3 ? 0x4ddbd0u : 0x4ddc60u;   /* 0x4a6824 */
    uint32_t entry = table + (uint32_t)rd32(e + 0x28) * 0x14;
    if (!sound_play(exe_str(entry))) return 0;
    return exe_f32(entry + 0x10);
}

/* not ported yet: 0x4683c0 a vehicle's sound by number, 0x476580
   the engine's idle sound, 0x462d60 network play's, 0x46e220 a node's fire put out, 0x4763b0 and 0x476480
   its sounds stopped, 0x469cd0 the entity's collisions dropped, 0x47de00 its textures released, 0x42d800
   each part's */
static void sound(int id, Node *n) { (void)id, (void)n; }

void Vehicle_State(Node *n, float dt)
{
    uint8_t *d = n->data;
    uint32_t f = (uint32_t)rd32(d + 0x454);
    if ((f & 0x8000) && !(f & 0x4000)) Vehicle_Wreck(n, 0);
    if (f & 0x20) {
        if (rd32(d + 0x454) & 1) {
            sound(2, n);
            put32(d + 0x454, rd32(d + 0x454) & ~1);
        }
        f &= ~9u;
    } else {
        Vehicle_Lights(n, rd32(d + 0x100));
        put32(d + 0x100, 0);
        if (rd32(d + 0xec)) {
            uint32_t now = (uint32_t)rd32(d + 0x454);
            if (f & 1) {
                if (now & 1) {
                    sound(2, n);
                    put32(d + 0x454, rd32(d + 0x454) & ~1);
                }
            } else if (!(now & 9)) {
                putf(d + 0x450, Engine_StartTime(n));
                put32(d + 0x454, (int32_t)((uint32_t)rd32(d + 0x454) | 0x80000008u));
            }
        }
        put32(d + 0xec, 0);
    }
    if (!(f & 0x80000000u)) return;
    putf(d + 0x450, fpu_f32(fpu_sub(rdf(d + 0x450), dt)));
    if (rd32(d + 0x450) <= 0) {
        put32(d + 0x450, 0);
        f &= 0x7fffffff;
        if ((f & 0x8c08) == 8) {   /* the engine started */
            sound(0xf, n);
            f |= 1;
            sound(1, n);
            /* 0x476580: the idle sound */
        } else if ((f & 0x8c08) == 0x8000) {   /* the wreck's time is up */
            f &= ~0x8000u;
            if (f & 0x100000) return;   /* network play (0x462d60); the flags stay as they were */
            Entity *e = Entity_OfNode(n);
            if (!(n->flags & 0x10)) {
                if (rd32(d + 0x454) & 0x4000) {
                    /* 0x46e220: its fire */
                    put32(d + 0x454, rd32(d + 0x454) & ~0x40);
                    Vehicle_Lights(n, 0);
                    if (n->flags & 0x800) Light_Remove(n, &n->flags);
                }
                /* 0x407370 (0x4a8760) and network play choose between hiding the wreck and freeing its
                   entity (0x423eb0, 0x455370, 0x41f4b0); single play hides it */
                if (e && e->node) {
                    Node_SetFlags(e->node, 1);
                    /* and 0x4763b0, 0x476480, 0x469cd0, 0x47de00, 0x42d800 (not ported yet) */
                }
            }
        }
        f &= 0xffff73f7u;
    }
    put32(d + 0x454, (int32_t)f);
}

/* 0x42d920: a special's kind (its data's +0); 0x42d940: it still works (+4 above 0, or +0x10 set) */
static int special_kind(const Node *s) { return s ? rd32(s->data) : 0; }
static int special_works(const Node *s) { return s && (rd32(s->data + 4) >= 1 || rd32(s->data + 0x10) != 0); }

/* a working special of the kind among the three (+0x3ec, +0x3f0, +0x3f4) */
static int has_special(uint8_t *d, int kind)
{
    for (uint32_t o = 0x3ec; o <= 0x3f4; o += 4) {
        Node *s = slot(d, o);
        if (special_kind(s) == kind && special_works(s)) return 1;
    }
    return 0;
}

static uint32_t rdu(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static int32_t ftol(double x) { return (int32_t)(int64_t)x; }

/* the packet's amounts the mask selects, summed */
static int32_t packet_sum(const uint8_t *p)
{
    uint16_t m;
    memcpy(&m, p + 4, 2);
    int32_t s = 0;
    for (int i = 0; i < 4; i++)
        if (m & (1u << i)) s += rd32(p + 8 + 4 * i);
    return s;
}

/* a part's points (at off) against an amount at a: the part takes a / k, or when it has fewer than that
   (by ftol(points k), unsigned) it goes to 0 with its pair (off2) and the rest stays in the packet */
static void absorb(uint8_t *d, uint32_t off, uint32_t off2, uint8_t *a, float k)
{
    int32_t pts = rd32(d + off);
    uint32_t v = (uint32_t)ftol(fpu_mul((double)pts, k)), c = rdu(a);
    if (v < c) {
        put32(a, (int32_t)(c - v));
        put32(d + off, 0);
        put32(d + off2, 0);
    } else {
        put32(d + off, pts - ftol(fpu_div((double)c, k)));
        put32(d + off2, rd32(d + off2) - ftol(fpu_div((double)rdu(a), k)));
        put32(a, 0);
    }
}

int32_t Damage_Side(uint8_t *d, uint32_t side, uint8_t *p, int user)
{
    float ka = 1.0f, kc = 1.0f;
    if (side >= 5) return 0;
    if (side == 0 || side == 3) ka = has_special(d, 5) ? 2.0f : 1.0f;   /* 0x4a59ac, 0x4a59a4 */
    if (has_special(d, 7)) ka = fpu_f32(fpu_add(ka, 0.1f));             /* 0x4a59cc */
    if (has_special(d, 6)) {
        uint32_t b = 0x3f8ccccd;   /* 1.1 */
        memcpy(&kc, &b, 4);
    }
    uint16_t m;
    memcpy(&m, p + 4, 2);
    if (side == 4) {
        /* underneath: the armour (+0x19c) and the chassis (+0x188) take their share and the packet keeps
           all of it */
        if (m & 8) {
            int32_t pts = rd32(d + 0x19c);
            uint32_t c = rdu(p + 0x14);
            if ((uint32_t)ftol(fpu_mul((double)pts, ka)) < c) put32(d + 0x19c, 0);
            else put32(d + 0x19c, pts - ftol(fpu_div((double)c, ka)));
        }
        for (int i = 0; i < 4; i++) {
            if (!(m & 3)) continue;
            int32_t pts = rd32(d + 0x188);
            uint32_t c = rdu(p + 8 + 4 * i);
            if ((uint32_t)ftol(fpu_mul((double)pts, kc)) < c) put32(d + 0x188, 0);
            else put32(d + 0x188, pts - ftol(fpu_div((double)c, kc)));
        }
        return packet_sum(p);
    }
    /* the shell's options for the user's vehicle (0x5e14e4): 0x10 its armour, 8 its chassis take nothing
       and the packet's share is dropped (outside network play) */
    int no_armour = (g_game_options & 0x10) && user, no_chassis = (g_game_options & 8) && user;
    if ((m & 8) && !no_armour) absorb(d, 0x148 + side * 4, 0x18c + side * 4, p + 0x14, ka);
    else if (no_armour) put32(p + 0x14, 0);
    for (int i = 0; i < 4; i++) {
        if ((m & 3) && !no_chassis) absorb(d, 0x138 + side * 4, 0x178 + side * 4, p + 8 + 4 * i, kc);
        else if (no_chassis) put32(p + 8 + 4 * i, 0);
    }
    return packet_sum(p);
}

/* the weapon instance of a gun's node (0x456890, 0x4568d0's search), NULL for none */
static WeaponInst *weapon_of(const Node *gun)
{
    for (int i = 0; i < g_weapon_count; i++)
        if (g_weapons[i].gun == gun) return &g_weapons[i];
    return NULL;
}

/* the amounts the mask selects, each in turn against the points at hp (signed): a larger amount loses the
   points, which go to 0, and the handler stops with 1; a smaller one is taken whole */
static int take(uint8_t *p, uint8_t *hp, int clear_amount)
{
    uint16_t m;
    memcpy(&m, p + 4, 2);
    for (int i = 0; i < 4; i++) {
        if (!(m & (1u << i))) continue;
        int32_t a = rd32(p + 8 + 4 * i), h = rd32(hp);
        if (a > h) {
            put32(p + 8 + 4 * i, a - h);
            put32(hp, 0);
            return 1;
        }
        put32(hp, h - a);
        if (clear_amount) put32(p + 8 + 4 * i, 0);
    }
    return 0;
}

/* 0x42f380, the engine (class 0x15): +0 the points of +4; +0x18 its power, their fraction, at least 0.275
   (0.4 for the user's or in network play) */
static int damage_engine(Node *n, uint8_t *p)
{
    uint8_t *e = n->data;
    uint32_t b = (Node_Root(n)->flags & 0x10) ? 0x3ecccccdu : 0x3e8ccccdu;
    float lo;
    memcpy(&lo, &b, 4);
    if (take(p, e, 1)) {
        putf(e + 0x18, lo);
        return 1;
    }
    putf(e + 0x18, fpu_f32(fpu_div((double)rd32(e), (double)rd32(e + 4))));
    if (rdf(e + 0x18) < lo || rdf(e + 0x18) != rdf(e + 0x18)) putf(e + 0x18, lo);
    return 0;
}

/* 0x4303f0, class 0x16: +4 the points of +8; +0x10 the fraction times +0xc, at least 0.2 of +0xc */
static int damage_16(Node *n, uint8_t *p)
{
    uint8_t *e = n->data;
    float lo = fpu_f32(fpu_mul(rdf(e + 0xc), exe_f32(0x4a5cf8)));
    if (take(p, e + 4, 1)) {
        putf(e + 0x10, lo);
        return 1;
    }
    putf(e + 0x10, fpu_f32(fpu_mul(fpu_div((double)rd32(e + 4), (double)rd32(e + 8)), rdf(e + 0xc))));
    if (rdf(e + 0x10) < lo || rdf(e + 0x10) != rdf(e + 0x10)) putf(e + 0x10, lo);
    return 0;
}

/* 0x409be0: the AI's grip estimate (its block's +0xa994) from the four wheels' +0xc */
static void ai_grip(Node *root)
{
    uint8_t *d = root->data;
    uint8_t *ai = slot(d, 0x108) ? (uint8_t *)slot(d, 0x108) : NULL;
    float s = rdf(slot(d, 0x3a8)->data + 0xc);
    s = fpu_f32(fpu_add(rdf(slot(d, 0x3ac)->data + 0xc), s));
    s = fpu_f32(fpu_add(rdf(slot(d, 0x3b8)->data + 0xc), s));
    double avg = fpu_mul(fpu_add(rdf(slot(d, 0x3bc)->data + 0xc), s), exe_f32(0x4a51b0));
    if (!ai) return;
    if (!(avg >= exe_f32(0x4a5188))) putf(ai + 0xa994, 1.0f);
    else putf(ai + 0xa994, fpu_f32(fpu_sqrt(fpu_div(fpu_f32(avg), rdf(d + 0x110)))));
}

/* 0x42fca0, a wheel (class 0x1e): +4 the points of +8; its grip +0xc the fraction times +0x10, at least
   0.5 (by the bits), and +0x14 from +0x18 at whole to 1 at none; the AI told */
static int damage_wheel(Node *n, uint8_t *p)
{
    uint8_t *e = n->data;
    if (take(p, e + 4, 1)) {
        put32(e + 0xc, 0x3f000000);
        put32(e + 0x14, 0x40000000);
        ai_grip(Node_Root(n));
        return 1;
    }
    int32_t h = rd32(e + 4), m = rd32(e + 8);
    putf(e + 0xc, fpu_f32(fpu_mul(fpu_div((double)h, (double)m), rdf(e + 0x10))));
    if (rd32(e + 0xc) < 0x3f000000) put32(e + 0xc, 0x3f000000);
    double one_g = fpu_sub(exe_f32(0x4a5cd4), rdf(e + 0x18));
    putf(e + 0x14, fpu_f32(fpu_add(fpu_mul(fpu_div((double)(m - h), (double)m), one_g), rdf(e + 0x18))));
    ai_grip(Node_Root(n));
    return 0;
}

/* 0x430580, class 0x17: +0 the points of +4 (an armour amount scaled by +0x1c, plus 0.5, against them
   but the amount itself taken); +0xc from +0x10 at whole to 5 at none, +0x14 from 0.5 to +0x18 */
static int damage_17(Node *n, uint8_t *p)
{
    uint8_t *e = n->data;
    uint16_t m;
    memcpy(&m, p + 4, 2);
    const float half = exe_f32(0x4a5d00), five = exe_f32(0x4a5d04);
    for (int i = 0; i < 4; i++) {
        if (!(m & (1u << i))) continue;
        int32_t a = rd32(p + 8 + 4 * i), v = a;
        if (m & 8) v = (int32_t)(int64_t)fpu_add(fpu_mul((double)a, rdf(e + 0x1c)), half);
        if (v > rd32(e)) {
            put32(p + 8 + 4 * i, v - rd32(e));
            put32(e, 0);
            putf(e + 0xc, five);
            putf(e + 0x14, half);
            return 1;
        }
        put32(e, rd32(e) - a);
        put32(p + 8 + 4 * i, 0);
    }
    double r = fpu_div((double)rd32(e), (double)rd32(e + 4));
    putf(e + 0xc, fpu_f32(fpu_sub(five, fpu_mul(fpu_sub(five, rdf(e + 0x10)), r))));
    putf(e + 0x14, fpu_f32(fpu_add(fpu_mul(fpu_sub(rdf(e + 0x18), half), r), half)));
    return 0;
}

/* 0x434540, class 0x14: +0 the points (a test for the third amount's packet flag compares the bit with 3,
   never true) */
static int damage_14(Node *n, uint8_t *p) { return take(p, n->data, 1); }

/* 0x42d8a0, class 0x18: +8 the points; the amounts are left as they are; at none +4, +8 and +0x10 are 0 */
static int damage_18(Node *n, uint8_t *p)
{
    uint8_t *e = n->data;
    uint16_t m;
    memcpy(&m, p + 4, 2);
    for (int i = 0; i < 4; i++) {
        if (!(m & (1u << i))) continue;
        int32_t a = rd32(p + 8 + 4 * i), h = rd32(e + 8);
        if (a > h) {
            put32(e + 8, 0);
            put32(e + 4, 0);
            put32(e + 0x10, 0);
            return 1;
        }
        put32(e + 8, h - a);
    }
    return 0;
}

/* 0x456110, a gun (class 0x32): its instance's points (+0xc, unsigned); every amount goes on, 1 when one
   was more than was left */
static int damage_gun(Node *n, uint8_t *p)
{
    WeaponInst *w = weapon_of(n);
    if (!w) return 0;
    uint16_t m;
    memcpy(&m, p + 4, 2);
    int r = 0;
    for (int i = 0; i < 4; i++) {
        if (!(m & (1u << i))) continue;
        uint32_t a = rdu(p + 8 + 4 * i), h = (uint32_t)w->v0c;
        if (a > h) {
            put32(p + 8 + 4 * i, (int32_t)(a - h));
            r = 1;
            w->v0c = 0;
        } else {
            w->v0c = (int32_t)(h - a);
            put32(p + 8 + 4 * i, 0);
        }
    }
    return r;
}

int Class_Damage(Node *n, uint8_t *p, const float *at)
{
    int cls = n->cls;
    if (Class_IsStructure(cls) && cls != 4) cls = 2;
    switch (cls) {
    case 0x15: return damage_engine(n, p);
    case 0x16: return damage_16(n, p);
    case 0x1e: return damage_wheel(n, p);
    case 0x17: return damage_17(n, p);
    case 0x14: return damage_14(n, p);
    case 0x18: return damage_18(n, p);
    case 0x32: return damage_gun(n, p);
    case 1: return Vehicle_Damage(n, p, at);
    /* not ported yet: 2 0x430900 (structures), 4 0x434950, 9 0x431c60,
       8 0x4289f0, 7 0x432ed0, 0xa 0x4346b0 */
    default: return 0;
    }
}

/* a part's points now (first) and at whole (second), by its class (0x42bf90's and 0x42d0a0's switches) */
static int32_t part_points(const Node *n, int whole)
{
    if (!n) return 0;
    const uint8_t *e = n->data;
    switch (n->cls) {
    case 0x14: case 0x15: case 0x17: return rd32(e + (whole ? 4 : 0));
    case 0x16: case 0x1e: return rd32(e + (whole ? 8 : 4));
    case 0x18: return rd32(e + (whole ? 0xc : 8));
    case 0x32: {
        const WeaponInst *w = weapon_of(n);
        if (!w) return 0;
        return whole ? g_weapon_defs[w->def].v[1] : w->v0c;
    }
    default: return 0;
    }
}

double Vehicle_Whole(uint8_t *d)
{
    uint32_t now = 0, all = 0;
    for (uint32_t i = 0; i < 20; i++) {
        Node *n = slot(d, 0x3a8 + 4 * i);
        now += (uint32_t)part_points(n, 0);
        all += (uint32_t)part_points(n, 1);
    }
    return fpu_div((double)now, (double)all);
}

/* 0x482f20: the state on the node and every node under it (Lod_SetState); true if any changed */
/* 0x4286c0: the entity's cached sphere (+0x68 -1, +0x70 0) */
static void sphere_drop(Entity *e)
{
    if (e) {
        e->rest[(0x68 - 0x34) / 4] = 0xffffffffu;
        e->rest[(0x70 - 0x34) / 4] = 0;
    }
}

static bool state_tree(Node *n, int state)
{
    if (!n) return false;
    bool r = Lod_SetState(n, state);
    for (Node *c = n->child; c; c = c->next)
        if (state_tree(c, state)) r = true;
    return r;
}

/* 0x4301a0: a tyre blown, once: its radius (+0x1c) times 0x4a5cec, +0x44 set (the sound tblow.wav is not
   ported yet) */
static void tyre_blow(Node *w)
{
    uint8_t *e = w->data;
    if (rd32(e + 0x44)) return;
    putf(e + 0x1c, fpu_f32(fpu_mul(rdf(e + 0x1c), exe_f32(0x4a5cec))));
    put32(e + 0x44, 1);
}

void Damage_Look(uint8_t *d, uint32_t part, uint32_t level)
{
    /* the table 0x4d5428 is read at part before the bound (parts 5 and more are the wheels' own numbers
       plus 5, past its ten entries for the tyres' 6) */
    uint32_t t = exe_u32(0x4d5428 + part * 4);
    Node *tree;
    if (part >= 5) {
        Node *w = slot(d, 0x3a8 + t * 4);
        if (!w) return;
        tree = w->child;
    } else {
        tree = slot(d, 0x408 + t * 4);
        if (!tree) return;
    }
    int state = level <= 1 ? 0 : level == 2 ? 1 : level == 3 ? 2 : 3;
    if (part >= 5) {
        state = 0;
        if (level > 2) {
            state = 1;
            tyre_blow(slot(d, 0x3a8 + t * 4));
        }
    }
    if (tree && state_tree(tree, state)) sphere_drop((Entity *)slot(d, 0));
}

int32_t Damage_Parts(uint8_t *d, int side, uint8_t *p, int kill, float health)
{
    if (has_special(d, 9) && crt_rand() % 100 < 10) {   /* a working kind 9 turns a tenth away */
        memset(p + 4, 0, 2);
        return 0;
    }
    int part;
    int32_t sb;
    memcpy(&sb, &health, 4);
    if ((p[6] & 1) && sb < 0x42040000) {   /* aimed at the tyres, the vehicle below 33 % */
        part = 6;
        put32(p + 0x10, (crt_rand() & 0x32) + 0x4b);
    } else {
        do {
            part = rd32(d + 0x1bc + (uint32_t)(side * 0x11 + crt_rand() % rd32(d + 0x1a0 + side * 4)) * 4);
            if (part == -1) {
                memset(p + 4, 0, 2);
                return 0;
            }
        } while (!slot(d, 0x3a8 + (uint32_t)part * 4) || part == 6);
    }
    Node *n = slot(d, 0x3a8 + (uint32_t)part * 4);
    if (part_points(n, 0)) {
        int r = Class_Damage(n, p, NULL);
        n = slot(d, 0x3a8 + (uint32_t)part * 4);
        if (n->cls == 0x1e) {
            int32_t h, m;
            h = n->data ? rd32(n->data + 4) : 300;   /* 0x42fd90 */
            m = n->data ? rd32(n->data + 8) : 300;
            float f = m ? fpu_f32(fpu_div((double)h, (double)m)) : 0.0f;
            int32_t fb;
            memcpy(&fb, &f, 4);
            int level = fb > 0x3f400000 ? 0 : fb > 0x3f000000 ? 1 : fb > 0x3e800000 ? 2 : 3;
            Damage_Look(d, (uint32_t)part + 5, (uint32_t)level);
            /* the textures' swap after it (0x47c200, the names at 0x4d5470) is for parts below 5, never
               reached with part + 5 */
        }
        return r ? packet_sum(p) : 0;
    }
    if (kill) {
        float k = (Entity *)slot(d, 0) == g_user_entity ? 0.1f : 2.5f;
        int32_t sum = packet_sum(p);
        float r = (float)(crt_rand() % 100);
        double h = fpu_mul(Vehicle_Whole(d), r);
        if (fpu_mul((double)sum, k) > h) {
            put32(d + 0x454, rd32(d + 0x454) | (part == 6 ? 0x4020 : 0x20));
            return 0;
        }
    }
    int32_t v = part_points(slot(d, 0x3a8 + (uint32_t)part * 4), 1) >> 2;
    uint16_t m;
    memcpy(&m, p + 4, 2);
    for (int i = 0; i < 4; i++) {
        if (!(m & (1u << i))) continue;
        uint32_t a = rdu(p + 8 + 4 * i);
        if ((uint32_t)v < a) {
            put32(p + 8 + 4 * i, (int32_t)(a - (uint32_t)v));
            return 0;
        }
        put32(p + 8 + 4 * i, 0);
        v -= (int32_t)a;
    }
    return packet_sum(p);
}

/* a part's two counts by its getter (0x42f480 the engine's +0 / +4, 0x430650 class 0x17's +0 / +4,
   0x4304a0 class 0x16's +4 / +8, 0x42fd90 a wheel's +4 / +8; 300 each without data). Without the part
   the original leaves its locals as they were: the port takes 300 / 300 then */
static void counts(const Node *n, uint32_t off, int32_t *a, int32_t *b)
{
    *a = *b = 300;
    if (n && n->data) {
        *a = rd32(n->data + off);
        *b = rd32(n->data + off + 4);
    }
}

/* cur / max as the FPU makes it: 0 / 0 is the x87's default NaN, negative */
static float ratio(int32_t a, int32_t b)
{
    if (a == 0 && b == 0) { uint32_t u = 0xffc00000u; float f; memcpy(&f, &u, 4); return f; }
    return fpu_f32(fpu_div((double)a, (double)b));
}

static int32_t fb(float f) { int32_t v; memcpy(&v, &f, 4); return v; }

/* cur / max held to [0, 1] (below 0 by the unrounded value, above 1 by its bits) */
static float held(int32_t cur, int32_t max)
{
    double q = fpu_div((double)cur, (double)max);
    float r = (q < 0 || q != q) ? 0.0f : fpu_f32(q);
    return fb(r) > 0x3f800000 ? 1.0f : r;
}

double Vehicle_Health(Node *n)
{
    const float hundred = exe_f32(0x4a5208);
    switch (n->cls) {
    case 1: break;
    case 2: case 3: case 0xb: case 0xc: {
        float min = 3.4028235e38f;
        for (Node *c = n->child; c; c = c->next) {
            if (!Class_IsStructure(c->cls) || (c->flags & 0xf000) <= 0x1000 || !c->data) continue;
            int32_t m = rd32(c->data + 4);
            if (m <= 0) continue;
            double q = fpu_div((double)rd32(c->data), (double)m);
            if (q < min || q != q) min = fpu_f32(q);
        }
        return fpu_mul(min, hundred);
    }
    case 8: case 9: {
        const uint8_t *b = *Data_Ptr(n->data, 0x508);
        if (rd32(b + 4) < 1) return hundred;
        return fpu_mul(fpu_mul((double)rd32(b), rdf(b + 8)), hundred);
    }
    default: return hundred;
    }
    uint8_t *d = n->data;   /* 0x42d5e0; a dead vehicle in a network game (0x461900) reads +0x484 */
    static const struct { uint32_t slot, off; } parts[7] = {
        {0x3c4, 0}, {0x3c8, 0}, {0x3cc, 4}, {0x3b8, 4}, {0x3bc, 4}, {0x3a8, 4}, {0x3ac, 4}};
    float min = 3.4028235e38f;
    int broke = 0;
    for (int i = 0; i < 7; i++) {
        int32_t a, b;
        counts(slot(d, parts[i].slot), parts[i].off, &a, &b);
        float r = ratio(a, b);
        if (fb(r) < 0x3ca3d70a) { broke++; continue; }   /* below 0.02 by its bits, negatives too */
        if (fb(r) > 0x3f800000) r = 1.0f;
        if (r < min || r != r) min = r;
    }
    if (fb(min) >= 0x3f7ff972 && broke <= 0) {
        /* whole parts: the chassis's and the armour's sides decide. The armour's minimum is a local the
           original never sets (a quirk): with every armour maximum 0 it is what the stack held, the port
           takes FLT_MAX */
        float ma = 3.4028235e38f, mb = 3.4028235e38f;
        for (int i = 0; i < 4; i++) {
            if (rd32(d + 0x158 + 4 * i) > 0) {
                float r = held(rd32(d + 0x138 + 4 * i), rd32(d + 0x158 + 4 * i));
                if (ma > r) ma = r;
            }
            if (rd32(d + 0x168 + 4 * i) > 0) {
                float r = held(rd32(d + 0x148 + 4 * i), rd32(d + 0x168 + 4 * i));
                if (mb > r) mb = r;
            }
        }
        return fpu_add(fpu_mul(mb > ma ? ma : mb, exe_f32(0x4a5218)), exe_f32(0x4a521c));   /* 72, 28 */
    }
    switch (broke) {
    case 0: return fpu_add(fpu_mul(min, exe_f32(0x4a5224)), exe_f32(0x4a5228));   /* 13, 15 */
    case 1: return fpu_add(fpu_mul(min, exe_f32(0x4a522c)), exe_f32(0x4a5230));   /* 8, 5 */
    case 2: return fpu_mul(min, exe_f32(0x4a5230));                                /* 5 */
    default: return fpu_mul(min, exe_f32(0x4a5220));                               /* 0.1 */
    }
}

/* the smoke trails (0x46e170 start, 0x46e270 its density, 0x46e2b0 its kind, 0x46e220 stop): the
   effects are not ported yet */
static void smoke_start(uint8_t *d, float k) { (void)d, (void)k; }
static void smoke_density(uint8_t *d, float k) { (void)d, (void)k; }
static void smoke_kind(uint8_t *d, int kind) { (void)d, (void)kind; }
static void smoke_stop(uint8_t *d) { (void)d; }

void Vehicle_Smoke(uint8_t *d, float health)
{
    int32_t h = fb(health);
    uint32_t flags = (uint32_t)rd32(d + 0x454);
    if (h >= 0x3f400000) {   /* 0.75 and up: no smoke */
        if (flags & 0x100) {
            smoke_stop(d);
            put32(d + 0x454, (int32_t)((uint32_t)rd32(d + 0x454) & ~0x100u));
        }
        return;
    }
    float k;
    int kind;
    if (h > 0x3f19999a) k = 0, kind = 0;          /* above 0.6 */
    else if (h > 0x3ecccccd) k = 0.5f, kind = 1;  /* above 0.4 */
    else k = 1.0f, kind = 2;
    if (!(flags & 0x100)) {
        smoke_start(d, k);   /* at +0xa4, the body part +0x410's +0x9c */
        put32(d + 0x454, (int32_t)((uint32_t)rd32(d + 0x454) | 0x100));
    } else
        smoke_density(d, k);
    smoke_kind(d, kind);
}

void Terrain_Wear(Node *n, float amount)
{
    uint8_t *d = n->data;
    if (rd32(d + 0x454) & 0x20) return;
    /* 0x45dbb0 reads 0x4dc464, which nothing writes: 0 */
    float acc = fpu_f32(fpu_add(rdf(d + 0x458), amount));
    putf(d + 0x458, acc);
    /* the packet: the armour's amount only (mask 8); the rest of it is not set, and unread with that
       mask and a speed of 100 */
    uint8_t p[0x18];
    memset(p, 0, sizeof p);
    p[4] = 8;
    int32_t whole = ftol(acc);
    put32(p + 0x14, whole);
    putf(d + 0x458, fpu_f32(fpu_sub(acc, (double)(uint32_t)whole)));   /* the count as an unsigned qword */
    int r = crt_rand() % 6;
    static const uint8_t sides[6] = {0, 1, 1, 2, 2, 3};
    int32_t left = Damage_Side(d, sides[r], p, (n->flags & 0x10) != 0);
    while (left) left = Damage_Parts(d, r, p, 0, 100.0f);
    double h = fpu_mul(Vehicle_Health(n), exe_f32(0x4a59fc));
    if (h < 1.0 || h != h) Vehicle_Smoke(d, fpu_f32(h));
}

float Side_Whole(const uint8_t *d, uint32_t side)
{
    float a = side != 4 ? whole(d, 0x178 + 4 * side, 0x158 + 4 * side) : fpu_f32(fpu_mul((double)rd32(d + 0x188), exe_f32(0x4a59fc)));
    float b = side != 4 ? whole(d, 0x18c + 4 * side, 0x168 + 4 * side) : fpu_f32(fpu_mul((double)rd32(d + 0x19c), exe_f32(0x4a59fc)));
    return b > a ? a : b;
}

/* not ported yet: the sparks (0x45c780, "CHUNK4" at the point), the death's other steps (0x46fdb0, the
   AI's death 0x488950, the guns' 0x455370, the engine's sound 0x476440, the death camera 0x407010 with
   0xfefe, the HUD 0x428890) */
static void sparks(const float at[3]) { (void)at; }
static void death_steps(Node *n, int user) { (void)n, (void)user; }

int Vehicle_Damage(Node *n, uint8_t *p, const float *at)
{
    uint8_t *d = n->data;
    if (p && (n->flags & 0x20)) return 0;
    int was_dead = (rd32(d + 0x454) & 0x20) != 0;
    if (p && was_dead) return 0;
    if (at) {
        float w[3];
        Frame_Points(&w, (const float (*)[3])at, 1, &n->frame);   /* 0x452740 */
        sparks(w);
    }
    if (p) {
        /* the side and the list by the point's direction */
        double s = Fast_InvSqrt(fpu_add(fpu_mul(at[0], at[0]), fpu_add(fpu_mul(at[1], at[1]), fpu_mul(at[2], at[2]))));
        float nx = fpu_f32(fpu_mul(at[0], s)), ny = fpu_f32(fpu_mul(at[1], s)), nz = fpu_f32(fpu_mul(s, at[2]));
        uint32_t side, list;
        double s2 = 0;
        float u = 0, w = 0;
        double wq = 0;
        if (fb(ny) <= 0x3f4ccccd) {
            s2 = Fast_InvSqrt(fpu_add(fpu_mul(nz, nz), fpu_mul(nx, nx)));
            u = fpu_f32(fpu_mul(nx, s2));
            wq = fpu_mul(s2, nz);
            w = fpu_f32(wq);
        }
        if (fb(ny) > 0x3f4ccccd) side = 4;
        else if (fb(u) > 0x3f34fdf4) side = 2;
        else if ((uint32_t)fb(u) > 0xbf34fdf4u) side = 1;
        else side = fb(w) > 0x3f34fdf4 ? 0 : 3;
        /* the list's split is made again, the same; its first test on the unrounded value */
        if (fb(ny) > 0x3f4ccccd) list = 6;
        else if (wq > exe_f32(0x4a59f4)) list = 0;
        else if ((uint32_t)fb(w) > 0xbf34fdf4u) list = 5;
        else if (fb(w) > 0) list = fb(u) > 0 ? 3 : 1;
        else list = fb(u) > 0 ? 4 : 2;
        float m = Side_Whole(d, side);   /* inlined in the original */
        if ((p[6] & 1) && Vehicle_Health(n) > exe_f32(0x4a5a00)) {   /* 33 */
            uint16_t one = 1;
            memcpy(p + 4, &one, 2);
            put32(p + 8, 5);
        }
        int32_t left = Damage_Side(d, side, p, (n->flags & 0x10) != 0);
        if (side == 0) {
            float f = Side_Whole(d, 0);
            if ((f < exe_f32(0x4a5a04) || f != f) && fb(m) > 0x3f000000) Light_SetOn(n, 0);   /* 0.5 */
        }
        while (left) left = Damage_Parts(d, (int)list, p, 1, fpu_f32(Vehicle_Health(n)));
        if (!was_dead && side < 5) {
            float v = Side_Whole(d, side);
            int level = v > exe_f32(0x4a5a10) ? 0 : fb(v) > 0x3f000000 ? 1 : fb(v) > 0x3e800000 ? 2 : 3;
            Damage_Look(d, side, (uint32_t)level);
            /* the body part's textures: the frame 0, 1, 1, 2 by the level */
            Node *part = slot(d, 0x408 + exe_u32(0x4d5428 + 4 * side) * 4);
            static const int frame[4] = {0, 1, 1, 2};
            int count = (int32_t)exe_u32(0x4d5458 + 4 * side);
            for (int i = 0; i < count; i++)
                Tex_SetFrame((uintptr_t)part, exe_str(0x4d5470 + (side * 10 + (uint32_t)i) * 13), 0, frame[level], true);
        }
    }
    double h = fpu_mul(Vehicle_Health(n), exe_f32(0x4a59fc));
    if (h < 1.0 || h != h) Vehicle_Smoke(d, fpu_f32(h));
    if (!(rd32(d + 0x454) & 0x20)) return 0;
    /* the death; network play's messages (0x461900, "%s has been killed") are not ported */
    if (rd32(d + 0x454) & 0x4000) {
        putf(d + 0x450, 10.0f);
        put32(d + 0x454, (int32_t)((uint32_t)rd32(d + 0x454) | 0x80008000u));
        death_steps(n, (n->flags & 0x10) != 0);
        AI_Rethink();   /* 0x408bf0 */
        sound(2, n);   /* 0x4683c0 */
        Node_SetFlags(n, 0x200);
        if (g_user_entity && g_user_entity->node == n) {
            g_input_menu = 0;   /* 0x480680 */
            int32_t eight = 8;
            memcpy(INPUT_AT(0x4def9c), &eight, 4);
        }
    } else
        Vehicle_Wreck(n, 1);
    /* a kill by the user scores in the melee (0x4072e0) while 0x4a8760 is set (0x407030); never in a
       mission */
    return 1;
}

/* not ported yet: the user's knock in the cockpit (0x433240 on the eye node, 0x452cd0 the screen's shake)
   and the HUD's damage arrows (0x468a70) */
static void knock(Node *n, const float nv[3], float k) { (void)n, (void)nv, (void)k; }

int Impact_Damage(Node *n, Node *src, const float v[3], const float nv[3], const float *at)
{
    /* 0x45dbb0 (0x4dc464) is never set: the user is never spared */
    uint8_t p[0x1c];
    memset(p, 0, sizeof p);
    int kind = 0;
    if (src && (src->cls == 0x33 || src->cls == 0x34)) {
        /* a projectile's (0x45ced0, 0x45ce90 kind 0x12 adds +6 bit 0) or a 0x34's (0x4543a0) packet: the
           weapons are not ported yet, so none */
    } else {
        uint16_t eight = 8;
        memcpy(p + 4, &eight, 2);
    }
    if (p[4] & 8) {
        float b[3] = {-nv[0], -nv[1], -nv[2]};
        uint8_t body[0x3c];
        int moving = src && (src->cls == 1 || src->cls == 7 || src->cls == 9);   /* 0x42e640 */
        Body_State(body, moving ? src : n);
        double dot = fpu_add(fpu_mul(v[0], b[0]), fpu_add(fpu_mul(v[1], b[1]), fpu_mul(v[2], b[2])));
        double e = fpu_mul(fpu_mul(dot, dot), rdf(body));
        uint32_t amount = (uint32_t)ftol(fpu_mul(e, exe_f32(moving ? 0x4a62c0 : 0x4a62c4)));
        put32(p + 0x14, (int32_t)amount);
        if ((n->flags & 0x10) && amount > 0x14)
            knock(n, nv, fpu_f32(fpu_mul((double)amount, exe_f32(0x4a62c8))));   /* 0.02 */
    }
    put32(p, 0);
    if (!src) kind = 4;
    else if (src->cls == 1 || src->cls == 8 || src->cls == 9) kind = 1;   /* the source in +0, not kept here */
    else if (Class_IsStructure(src->cls)) kind = 2;
    else if (src->cls == 0x33) kind = (p[4] & 8) ? 0x18 : 0x10;
    else if (src->cls == 0x34) kind = 0x10;
    (void)kind;
    Node_Ref(n);
    Class_Damage(n, p, at);
    Node *live = Node_Live(n);
    if (live) Node_Unref(live);
    /* with a source, the kill's credit and messages (0x40fc80 with the kind) are not ported yet */
    return live != NULL;
}

/* the wreck's effects, not ported yet: the explosions (0x4540d0 "x1_carx1" at the vehicle, "x1_cars1"
   near it), the sound's pitch (0x476170), a wheel or a body piece thrown off (0x45b760; a piece as a
   "v_chnk" object taking the part's class 0x3f children), the fires on the children (0x45b3f0) and the
   smoke (0x45c780, 0x4d538c or 0x4d5394), the 0x46fdb0 list */
static void wreck_fx(Node *n, int what, int a) { (void)n, (void)what, (void)a; }

/* the next part from a random start among count slots from first that is there and not hidden (bit 1);
   -1 after a full turn */
static int wreck_part(uint8_t *d, int first, int count)
{
    int start = crt_rand() % count, i = start;
    for (;;) {
        Node *p = slot(d, 0x3a8 + (uint32_t)(first + i) * 4);
        if (p && !(p->flags & 1)) return first + i;
        i = (i + 1) % count;
        if (i == start) return -1;
    }
}

void Vehicle_Wreck(Node *n, int start)
{
    uint8_t *d = n->data;
    sphere_drop((Entity *)slot(d, 0));
    if (start) {
        putf(d + 0x450, 10.0f);
        put32(d + 0x454, (int32_t)((uint32_t)rd32(d + 0x454) | 0x80008000u));
        wreck_fx(n, 0, 0);       /* 0x46fdb0 */
        smoke_stop(d);           /* 0x46e220; the bit 0x100 stays */
        put32(d + 0x454, rd32(d + 0x454) & ~0x40);
        Vehicle_Lights(n, 0);
        if (n->flags & 0x800) Light_Remove(n, &n->flags);   /* 0x43a400 */
        Node *brake = slot(d, 0x3a4);
        if (brake) brake->flags |= 1;
        wreck_fx(n, 1, 0);       /* the explosion "x1_carx1" */
        int r1 = crt_rand();
        float pitch = fpu_f32(fpu_add(fpu_mul(fpu_mul((double)r1, exe_f32(0x4a5a3c)), exe_f32(0x4a59ac)), exe_f32(0x4a59ac)));
        int r2 = crt_rand();
        float k = fpu_f32(fpu_add(fpu_mul((double)r2, exe_f32(0x4a5a3c)), 1.0f));
        (void)pitch, (void)k;
        wreck_fx(n, 2, 0);       /* 0x476170 with both */
        /* every part at its worst look; the body's textures at frame 3 */
        for (uint32_t i = 0; i < 11; i++) {
            Damage_Look(d, i, 4);
            if (i < 5) {
                Node *part = slot(d, 0x408 + exe_u32(0x4d5428 + 4 * i) * 4);
                int count = (int32_t)exe_u32(0x4d5458 + 4 * i);
                for (int j = 0; j < count; j++)
                    Tex_SetFrame((uintptr_t)part, exe_str(0x4d5470 + (i * 10 + (uint32_t)j) * 13), 0, 3, true);
            }
        }
        Node_SetFlags(n, 0x200);
        sound(2, n);
        sound(7, n);
        if (g_user_entity && g_user_entity->node == n) {
            g_input_menu = 0;   /* 0x480680; the death camera (0x407010 with 0xfefe) and the HUD (0x428890) */
            int32_t eight = 8;
            memcpy(INPUT_AT(0x4def9c), &eight, 4);
        } else {
            /* outside network play every part takes a blow of its points times (129 + rand & 127) / 256 */
            for (uint32_t i = 0; i < 20; i++) {
                Node *p = slot(d, 0x3a8 + i * 4);
                if (!p) continue;
                uint8_t pk[0x1c];
                memset(pk, 0, sizeof pk);
                pk[4] = 2;
                int32_t pts = part_points(p, 0);
                put32(pk + 0xc, (((crt_rand() & 0x7f) + 0x81) * pts) >> 8);
                Class_Damage(p, pk, NULL);
            }
        }
        if (!(n->flags & 0x10)) wreck_fx(n, 3, 0);   /* the AI's death, 0x488950 */
        wreck_fx(n, 4, 0);       /* the guns, 0x455370 */
        AI_Rethink();            /* 0x408bf0 */
    }
    /* one of sixteen each step */
    switch (crt_rand() & 0xf) {
    case 0: {   /* a wheel (0-4) thrown off */
        int i = wreck_part(d, 0, 5);
        if (i < 0) return;
        wreck_fx(n, 5, i);
        slot(d, 0x3a8 + (uint32_t)i * 4)->flags |= 1;
        return;
    }
    case 1: {   /* a body piece (10-15) as a "v_chnk" node at the part's place, with its 0x3f children */
        int i = wreck_part(d, 10, 6);
        if (i < 0) return;
        Node *part = slot(d, 0x3a8 + (uint32_t)i * 4);
        Node *chunk = Node_Create(NULL);   /* 0x41f320, tagged "v_chnk" (0x4d57cc) */
        Node_FrameIn(&chunk->frame, part, NULL);
        chunk->flags = 0;
        chunk->geom = NULL;
        for (Node *c = part->child, *next; c; c = next) {
            next = c->next;
            if (c->cls == 0x3f) {
                Node_Detach(c);
                Node_Attach(c, chunk);
            }
        }
        wreck_fx(chunk, 6, i);   /* thrown off along the part's direction x 15 */
        part->flags |= 1;
        return;
    }
    case 2: case 3:   /* fire on each child at one in two */
        for (Node *c = n->child; c; c = c->next)
            if (crt_rand() & 1) wreck_fx(n, 7, 0);
        return;
    case 4: case 5:   /* smoke of one of two kinds */
        wreck_fx(n, 8, crt_rand() & 1);
        return;
    case 6: case 7: {   /* an explosion near, at three random offsets */
        for (int j = 0; j < 3; j++) (void)crt_rand();
        wreck_fx(n, 9, 0);
        return;
    }
    default: return;
    }
}
