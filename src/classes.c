/* The classes' handlers (classes.h). In the current rounding mode (render/fpu.h). */
#include "classes.h"
#include "cockpit.h"
#include "camera.h"
#include "radar.h"
#include "damage.h"
#include "clock.h"
#include "crt.h"
#include "effects.h"
#include "entity.h"
#include "lod.h"
#include "physics.h"
#include "terrain.h"
#include "texanim.h"
#include "weapons.h"
#include "exe.h"
#include "render/fpu.h"
#include <stdlib.h>
#include <string.h>

#define ADD fpu_add
#define MUL fpu_mul
#define SUB fpu_sub
#define DIV fpu_div
#define F32 fpu_f32

static void put32(uint8_t *p, int32_t v) { memcpy(p, &v, 4); }
static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }
static float rdf(const uint8_t *p) { float v; memcpy(&v, p, 4); return v; }
static void putf(uint8_t *p, float v) { memcpy(p, &v, 4); }

Node *Node_Root(Node *n)
{
    while (n->prev) n = n->prev;
    return n;
}

Entity *Entity_OfNode(Node *n)
{
    Node *r = Node_Root(n);
    for (int i = 0; i < MAX_ENTITIES; i++)
        if (g_entities[i].node == r) return &g_entities[i];
    return NULL;
}

/* ---- the start handlers ---- */

/* 0x4306c0 (structures; 0x434930 for class 4 when it is its own root): in network games only */
static void start_structure(Node *n) { (void)n; }

/* 0x428910 (artillery): the entity into the data's first word; the user's node is the target (0x5ad1a0) */
Node *g_artillery_target;   /* 0x5ad1a0 */
static void start_artillery(Node *n)
{
    Entity *e = Entity_OfNode(n);
    if (g_user_entity) g_artillery_target = g_user_entity->node;   /* and 0x5ad170 = 0 */
    *Data_Ptr(n->data, 0) = e;
}

/* 0x432b90 (class 7, a turret's base): a root takes its first class 7 descendant's data (0xa0 bytes),
   and that one leaves the class list; a non-root under a class 7 root has nothing to do. Then its place
   (+0x84 and +0x6c), its aim through the frame (+0x78) and the rate's inverse (+0x94). */
Node *Node_FindClass(Node *n, int cls)   /* 0x42e970 */
{
    if (!n) return NULL;
    if (n->cls == cls) return n;
    for (Node *c = n->child; c; c = c->next) {
        Node *f = Node_FindClass(c, cls);
        if (f) return f;
    }
    return NULL;
}

static void start_class7(Node *n)
{
    if (!n->prev) {
        Node *f = NULL;
        for (Node *c = n->child; c && !f; c = c->next) f = Node_FindClass(c, 7);
        if (!f) return;
        memcpy(n->data, f->data, 0xa0);
        Class_Unlist(f);
    } else if (Node_Root(n)->cls == 7) {
        return;
    }
    uint8_t *d = n->data;
    for (int k = 0; k < 3; k++) {
        putf(d + 0x84 + 4 * k, F32(n->frame.pos[k]));
        putf(d + 0x6c + 4 * k, F32(n->frame.pos[k]));
    }
    float v[1][3];
    memcpy(v, d + 0x78, sizeof v);
    Frame_Points(v, (const float (*)[3])v, 1, &n->frame);   /* 0x452740 */
    memcpy(d + 0x78, v, sizeof v);
    putf(d + 0x94, F32(DIV(1.0f, rdf(d + 0x94))));   /* 0x4a5d6c */
}

/* 0x42f460 (class 21) */
static void start_class21(Node *n)
{
    putf(n->data + 0xc, F32(MUL(rdf(n->data + 0x14), exe_f32(0x4a5b10))));
}

/* 0x4263b0 (classes 35 and 36, the gauges' needles): into the vehicle's slot (the data's +0x40), the
   node's frame as the rest */
static void start_needle(Node *n)
{
    uint8_t *d = n->data;
    Node *r = Node_Root(n);
    if (r) *Data_Ptr(r->data, 0x3a8 + 4 * rd32(d + 0x40)) = n;
    memcpy(d, &n->frame, 0x40);
    put32(d + 0x44, 0);
    put32(d + 0x48, 0);
}

/* 0x42ed80 (class 5, the scrounge): the node is its own data; its children (0x550998) become class 5
   with the heading flag */
Node *g_scrounge_items;   /* 0x550998 */
static void start_scrounge(Node *n)
{
    if (!n) return;
    n->data = (uint8_t *)n;
    g_scrounge_items = n->child;
    for (Node *c = n->child; c; c = c->next) {
        c->flags |= 0x40;
        c->cls = 5;
    }
}

/* 0x433370 (class 41, a muzzle): its frame into the data, then 0, 0 and its root */
static void start_class41(Node *n)
{
    uint8_t *d = n->data;
    if (!d) return;
    memcpy(d, &n->frame, 0x40);
    put32(d + 0x40, 0);
    put32(d + 0x44, 0);
    *Data_Ptr(d, 0x48) = Node_Root(n);
}

/* 0x42feb0: a wheel's tread, turned with the wheel each frame (not ported yet) */
static void wheel_tread(Node *n, const char *name, void *arg) { (void)n, (void)name, (void)arg; }

/* 0x42fbc0 (class 30, a wheel): its rest height (+0x1c, +0x28) and the travel (+-0.25 of it); a tread
   animation (0x18 bytes: the next time, the step 0.05, 12 frames) on the first texture of its intact
   and its damaged mesh */
static void start_wheel(Node *n)
{
    uint8_t *d = n->data;
    float y = F32(n->frame.pos[1]);
    putf(d + 0x28, y);
    putf(d + 0x1c, y);
    putf(d + 0x38, F32(MUL(rdf(d + 0x1c), 0.25f)));    /* 0x4a5cb8 */
    putf(d + 0x3c, F32(MUL(rdf(d + 0x1c), -0.25f)));   /* 0x4a5cbc */
    uint8_t *rec = calloc(1, 0x18);   /* 0x41fb80: the game heap */
    putf(rec, F32(ADD(g_clock_time, 0.05f)));            /* 0x4a5ccc */
    put32(rec + 4, 0x3d4ccccd);
    put32(rec + 8, 12);
    put32(rec + 0x14, 1);
    for (int state = 1; state >= 0; state--) {
        Lod_SetState(n->child, state);
        const char *t = Node_FirstTexture(n->child, 0);
        if (t && *t) TexAnim_Add(n, t, wheel_tread, 0x42feb0, rec);
    }
    *Data_Ptr(d, 0x84) = rec;
}

/* 0x434430: class 31's frames (not ported yet) */
static void class31_anim(Node *n, const char *name, void *arg) { (void)n, (void)name, (void)arg; }

/* 0x4342f0 (class 31): a record {0x24, 9, 9, 9} (the game heap) at the data's first word, animated on
   the user's vehicle */
static void start_class31(Node *n)
{
    uint8_t *d = n->data;
    uint8_t *rec = calloc(1, 0x10);
    put32(rec, 0x24);
    put32(rec + 4, 9);
    put32(rec + 8, 9);
    if (g_user_entity && g_user_entity->node == Node_Root(n)) {
        const char *t = Node_FirstTexture(n, 0);
        if (t && *t) TexAnim_Add(n, t, class31_anim, 0x434430, rec);
    }
    *Data_Ptr(d, 0) = rec;
    put32(rec + 8, 9);
    put32(rec + 4, 9);
    put32(rec + 0xc, 9);
}

/* 0x433180 (class 40): cleared (0xb8 bytes), its root, its place 0.6 below the node, 0.6 at +0xc, and three
   filters (damping 0.5, period 4) at +0x40, +0x68, +0x90 */
static void start_class40(Node *n)
{
    uint8_t *d = n->data;
    if (!d) return;
    memset(d, 0, 0xb8);
    *Data_Ptr(d, 0) = Node_Root(n);
    putf(d + 0x20, F32(n->frame.pos[0]));
    putf(d + 0x24, F32(SUB(n->frame.pos[1], 0.6f)));   /* 0x4a5d8c */
    putf(d + 0x28, F32(n->frame.pos[2]));
    put32(d + 0xc, 0x3f19999a);
    Filter_Init(d + 0x40, 0.5f, 4.0f);
    Filter_Init(d + 0x68, 0.5f, 4.0f);
    Filter_Init(d + 0x90, 0.5f, 4.0f);
}

/* 0x476240 (through 0x4761b0): the engine's sound, its channel at the data's +0x10c; no audio yet */
static void engine_sound(Node *n) { (void)n; }

/* ---- class 1, a vehicle (0x4294e0) ---- */

/* the hit zones: seven lists (counts at +0x1a0, 17 entries each at +0x1bc) of the parts a hit in that zone
   can damage; part goes into each zone of mask */
static void zone_add(uint8_t *d, int32_t part, uint32_t mask)
{
    for (int z = 0; z < 7; z++)
        if (mask & 1u << z) {
            int32_t k = rd32(d + 0x1a0 + 4 * z);
            put32(d + 0x1bc + 4 * (uint32_t)(17 * z + k), part);
            put32(d + 0x1a0 + 4 * z, k + 1);
        }
}

/* 0x455140: the hard point of the weapon whose gun is node: its side (+8), type (+0xc) and the x of its
   place; 0 for a node that is no weapon's (the original's 3) */
static int weapon_of(const Node *node, int32_t *side, int32_t *type, double *x)
{
    for (int i = 0; i < g_weapon_count; i++)
        if (g_weapons[i].gun == node) {
            const HardSlot *s = &g_weapons[i].hp->slot[g_weapons[i].slot];
            *side = s->side;
            *type = s->type;
            *x = s->frame.pos[0];
            return 1;
        }
    return 0;
}

/* a weapon's zones by its hard point's place: the type 3 ones everywhere; type 1 (a turret) all but one
   side's; else by the side (1 the front, 2 the rear, 3 and 4 the two others) and left or right */
static uint32_t weapon_zones(int32_t side, int32_t type, double x)
{
    int neg = x < 0, pos = x > 0 || x != x;   /* fcomp's flags against 0 */
    if (type == 3) return 0x7f;
    if (type == 1) return neg ? 0x67 : pos ? 0x79 : 0x7f;
    switch (side) {
    case 1: return neg ? 3 : pos ? 9 : 1;
    case 2: return type == 4 ? 0x20 : neg ? 0x24 : pos ? 0x30 : 0x20;
    case 3: return 0x10;
    case 4: return 4;
    }
    return 0;
}

/* _ftol of v by k */
static void scale(uint8_t *p, float k) { put32(p, (int32_t)(int64_t)MUL((double)rd32(p), k)); }

/* the user's set-up at the start: the cockpit (0x426410), the camera (0x403360), the weapons' display
   (0x4561f0), 0x42dc00 and the dashboard (0x471060); the cockpit (in part), the camera and the weapons'
   display are ported */
static void user_start(Node *n)
{
    Cockpit_Setup(n);   /* 0x426410 */
    if (g_screen) View_Init(1, g_screen);   /* 0x403360: the pilot's view again on the user */
    Weapons_DisplayStart(1);
}

static void start_vehicle(Node *n)
{
    uint8_t *d = n->data;
    float t[3];
    Timer_Init(t, 0.05f);   /* 0x3d4ccccd */
    memcpy(d + 0x444, t, sizeof t);
    Vehicle_PhysInit(n);
    if (n->cls == 1) {
        put32(d + 0x454, rd32(d + 0x454) | 0x80000);
        for (uint32_t i = 0; i < 24; i++) {
            Node *p = *Data_Ptr(d, 0x3a8 + 4 * i);
            if (p) Node_Ref(p);
        }
    }
    zone_add(d, 0, 9);
    zone_add(d, 1, 3);
    zone_add(d, 4, 0x30);
    zone_add(d, 5, 0x24);
    if (*Data_Ptr(d, 0x3b4)) {   /* the middle wheels */
        zone_add(d, 2, 0x10);
        zone_add(d, 3, 4);
    }
    zone_add(d, 6, 0x40);
    zone_add(d, 7, 0xb);
    zone_add(d, 8, 0x3f);
    zone_add(d, 9, 0x3f);
    for (uint32_t i = 10; i < 16; i++) {   /* the weapons */
        Node *p = *Data_Ptr(d, 0x3a8 + 4 * i);
        int32_t side, type;
        double x;
        if (p && weapon_of(p, &side, &type, &x)) zone_add(d, (int32_t)i, weapon_zones(side, type, x));
    }
    for (uint32_t i = 17; i < 19; i++)   /* the specials: one zone at random */
        if (*Data_Ptr(d, 0x3a8 + 4 * i)) zone_add(d, (int32_t)i, 1u << (crt_rand() % 7));
    zone_add(d, -1, 0x7f);
    zone_add(d, -1, 0x40);
    zone_add(d, -1, 0x40);
    if (!(n->flags & 0x10)) return;   /* Node_IsUser 0x42e620 */
    {   /* outside a network game (0x461900) the user's armour counts double and its parts' four times */
        for (uint32_t i = 0; i < 4; i++) {
            scale(d + 0x158 + 4 * i, 2.0f);   /* 0x4a59ac */
            scale(d + 0x138 + 4 * i, 2.0f);
            scale(d + 0x168 + 4 * i, 2.0f);
            scale(d + 0x148 + 4 * i, 2.0f);
        }
        for (uint32_t i = 0; i < 5; i++) {
            scale(d + 0x178 + 4 * i, 2.0f);
            scale(d + 0x18c + 4 * i, 2.0f);
        }
        for (uint32_t i = 0; i < 24; i++) {
            Node *p = *Data_Ptr(d, 0x3a8 + 4 * i);
            if (!p) continue;
            static const uint8_t words[][2] = {{0, 4}, {0, 4}, {4, 8}, {0, 4}, {8, 0xc}};   /* 20-24 */
            const uint8_t *w = p->cls >= 20 && p->cls <= 24 ? words[p->cls - 20] : p->cls == 30 ? words[2] : NULL;
            if (!w) continue;
            scale(p->data + w[0], 4.0f);   /* 0x4a59e0 */
            scale(p->data + w[1], 4.0f);
        }
    }
    user_start(n);
}

/* 0x4313f0 (class 9): its entity in the data's first word; the 0x508 block's two words from the armour
   (+0x138); the engine's sound; not below the ground */
static void start_class9(Node *n)
{
    uint8_t *d = n->data;
    uint8_t *b = *Data_Ptr(d, 0x508);
    *Data_Ptr(d, 0) = Entity_OfNode(n);
    put32(b + 4, rd32(d + 0x138));
    put32(b, rd32(d + 0x138));
    engine_sound(n);
    double h = Terrain_Height(n->frame.pos[0], n->frame.pos[2]);
    if (n->frame.pos[1] < h) n->frame.pos[1] = h;
    if (n->flags & 0x10) user_start(n);   /* 0x42e620 */
}

static void no_start(Node *n) { (void)n; }

/* 0x475ee0: the tyres' sounds and a flat's damage ("tflat.wav", "fdmg1.wav"): not ported yet */
static void tyre_step(Node *n, uint32_t flags, int32_t tclass) { (void)n, (void)flags, (void)tclass; }

/* 0x429c70: a vehicle's frame. Near the camera (within 25) the physics in sub-steps of at most 0.05 (the
   timer +0x444), each with the damage, until a step leaves the node changed (+8); away from it (or dying)
   one cheap step a frame */
void Vehicle_Frame(Node *n)
{
    uint8_t *d = n->data;
    float *t = (float *)(d + 0x444);
    if (View_Distance(n, 25.0f) == 2 || (n->flags & 0x20)) {
        float dt = g_clock_dt;   /* 0x450e50 */
        Vehicle_State(n, dt);
        if (!(n->flags & 0x20)) Vehicle_FarStep(n, dt);
        Timer_Init(t, 0.05f);   /* 0x3d4ccccd */
    } else {
        if (rd32(d + 0x454) & 0x20000) Vehicle_Near(n);
        Timer_Count(t, g_clock_dt);
        for (;;) {
            float s = Timer_Next(t);
            if (!(s > 0)) break;
            Vehicle_State(n, s);
            Vehicle_Physics(n, s);
            if (n->state) return;
        }
    }
    tyre_step(n, (uint32_t)rd32(d + 0x454), rd32(d + 0x45c));
}

/* a per-frame handler not ported yet: present so that the class keeps its nodes listed */
static void later(Node *n) { (void)n; }

typedef void (*ClassFn)(Node *);
static const struct {
    int32_t cls;
    ClassFn start, update[2];
} handlers[] = {
    {2, start_structure, {NULL, NULL}},     /* 0x4306c0 */
    {4, start_structure, {NULL, NULL}},     /* 0x434930 */
    {1, start_vehicle, {Vehicle_Frame, later}},   /* 0x4294e0; 0x429c70, 0x429d70 */
    {9, start_class9, {later, NULL}},       /* 0x4313f0; 0x4314a0 */
    {8, start_artillery, {later, NULL}},    /* 0x428910; 0x428950 */
    {7, start_class7, {later, NULL}},       /* 0x432b90; 0x432c70 */
    {10, no_start, {later, NULL}},          /* 0x434640; 0x434650 */
    {21, start_class21, {NULL, NULL}},      /* 0x42f460 */
    {22, no_start, {NULL, NULL}},           /* 0x4303e0 */
    {30, start_wheel, {NULL, NULL}},        /* 0x42fbc0 */
    {23, no_start, {NULL, NULL}},           /* 0x430570 */
    {20, no_start, {NULL, NULL}},           /* 0x434530 */
    {35, start_needle, {NULL, NULL}},       /* 0x4263b0 */
    {36, start_needle, {NULL, NULL}},
    {34, Radar_Start, {NULL, Radar_Frame}}, /* 0x41fff0; 0x4202d0 */
    {52, no_start, {NULL, NULL}},           /* 0x454810 */
    {31, start_class31, {NULL, later}},     /* 0x4342f0; 0x4343c0 */
    {32, Window_Start, {NULL, NULL}},       /* 0x433810 */
    {33, Window_Start, {NULL, NULL}},
    {53, no_start, {later, NULL}},          /* 0x45bc90; 0x45bca0 */
    {5, start_scrounge, {NULL, NULL}},      /* 0x42ed80 */
    {41, start_class41, {NULL, NULL}},      /* 0x433370 */
    {40, start_class40, {NULL, later}},     /* 0x433180; 0x433330 */
};

static int handler_index(int cls)
{
    if (Class_IsStructure(cls) && cls != 4) cls = 2;
    for (size_t i = 0; i < sizeof handlers / sizeof handlers[0]; i++)
        if (handlers[i].cls == cls) return (int)i;
    return -1;
}

void Class_StartAll(void)
{
    for (Cell *k = g_class_cells, *next; k; k = next) {
        next = k->next;
        Cell *c = k->value;
        Node *n = Node_Live(c->value);
        if (n) {
            int i = handler_index(n->cls);
            if (i >= 0 && handlers[i].start) handlers[i].start(n);
            if (i < 0 || (!handlers[i].update[0] && !handlers[i].update[1])) {
                Node_Unref(c->value);
                n = NULL;
            }
        }
        if (!n) {
            List_Remove(&g_class_nodes, c);
            g_class_count[1]--;
        }
        List_Remove(&g_class_cells, k);
        g_class_count[0]--;
    }
}

void Class_Frame(int which)
{
    for (Cell *c = g_class_nodes, *next; c; c = next) {
        next = c->next;
        Node *n = Node_Live(c->value);
        if (!n) {   /* freed: off both lists */
            for (Cell *k = g_class_cells; k; k = k->next)
                if (k->value == c) {
                    List_Remove(&g_class_cells, k);
                    g_class_count[0]--;
                    break;
                }
            List_Remove(&g_class_nodes, c);
            g_class_count[1]--;
            continue;
        }
        int i = handler_index(n->cls);
        if (i >= 0 && handlers[i].update[which]) handlers[i].update[which](n);
    }
}
