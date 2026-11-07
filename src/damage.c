/* A vehicle's state from step to step (damage.h). In the current rounding mode (render/fpu.h). */
#include "damage.h"
#include "classes.h"
#include "entity.h"
#include "exe.h"
#include "render/fpu.h"
#include "render/light.h"
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

/* not ported yet: 0x42c580 the wreck (its parts and fire), 0x4683c0 a vehicle's sound by number, 0x476580
   the engine's idle sound, 0x462d60 network play's, 0x46e220 a node's fire put out, 0x4763b0 and 0x476480
   its sounds stopped, 0x469cd0 the entity's collisions dropped, 0x47de00 its textures released, 0x42d800
   each part's */
static void wreck(Node *n, int a) { (void)n, (void)a; }
static void sound(int id, Node *n) { (void)id, (void)n; }

void Vehicle_State(Node *n, float dt)
{
    uint8_t *d = n->data;
    uint32_t f = (uint32_t)rd32(d + 0x454);
    if ((f & 0x8000) && !(f & 0x4000)) wreck(n, 0);
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
