/* Entities and classes (entity.h). In the current rounding mode (render/fpu.h). */
#include "entity.h"
#include "radar.h"
#include "clock.h"
#include "crt.h"
#include "exe.h"
#include "weapons.h"
#include "platform.h"
#include "render/fpu.h"
#include "render/light.h"
#include "collide.h"
#include "math3d.h"
#include "physics.h"
#include "terrain.h"
#include "classes.h"
#include "lod.h"
#include "tex.h"
#include "objects.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define ADD fpu_add
#define MUL fpu_mul
#define F32 fpu_f32

Entity g_entities[MAX_ENTITIES];
int g_entity_count;
Entity *g_user_entity;
static EntityFn on_add[0x33];   /* 0x54f7b8 */
static int on_add_count;        /* 0x4d3b54 */
static EntityFn on_remove[0x38];   /* 0x54f6d8 */
static int on_remove_count;        /* 0x4d3b58 */

Cell *g_class_nodes, *g_class_cells;
int g_class_count[2];

void Entity_Init(void)
{
    memset(g_entities, 0, sizeof g_entities);
    g_entity_count = 0;
    g_user_entity = NULL;
    memset(on_add, 0, sizeof on_add);
    on_add_count = 0;
    memset(on_remove, 0, sizeof on_remove);
    on_remove_count = 0;
    /* the ids (0x547a58) are not ported yet */
    Names_Add(NULL, NULL);   /* 0x492ac0: a new table */
}

static void names_free(void);

/* Entities_Free 0x423c30: the name table emptied (0x492c10: its heap destroyed); each entity's removal
   callbacks run and its node, when still live (Node_Live), released: once, and once more out of its parent
   when it holds more than the entity's reference, so an attached node goes too. The user's entity
   (0x4d3b64) is left as it was (Entities_Init clears it); the port clears it here as well, so nothing reads
   a freed node before the next mission */
void Entities_Free(void)
{
    names_free();
    for (int i = 0; i < MAX_ENTITIES; i++) {
        Entity *e = &g_entities[i];
        if (e->node) {
            for (int k = 0; k < on_remove_count; k++) on_remove[k](e);
            Node *n = Node_Live(e->node);   /* tagged 0x4d3b9c, as the two below */
            if (n) {
                int more = n->refs >= 2;
                Node_Unref(n);
                if (more) Node_Free(n);
            }
        }
        /* a COLP record (has_a 1) is the original's to free (0x44e3e0); the port's belongs to its vehicle */
        memset(e, 0, sizeof *e);
    }
    g_user_entity = NULL;
    g_entity_count = 0;
}

void Entity_OnAdd(EntityFn fn) { on_add[on_add_count++] = fn; }
void Entity_OnRemove(EntityFn fn) { on_remove[on_remove_count++] = fn; }   /* 0x423d10 */

/* 0x423eb0: the removal callbacks in the order they were set: the collision's (0x469cd0), the AI's
   (0x408660), the radar's (0x41fef0), the artillery's (0x428d00), the spawn places' (0x463260), the
   level of detail's (0x4949e0: the node's mesh), the camera's (0x406d20) and the melee's opponents'
   (0x4073a0) */
void Entity_Remove(Entity *e)
{
    int i = (int)(e - g_entities);
    if (i < 0 || i >= MAX_ENTITIES) i = 0;
    for (int k = 0; k < on_remove_count; k++) on_remove[k](e);
    if (e->node && (e->node->flags & 0x10)) g_user_entity = NULL;   /* 0x42e620: the user's (0x4d3b64) */
    Node_Unref(e->node);   /* tagged 0x4d3b9c */
    /* a COLP record (has_a 1) is the original's to free (0x44e3e0); the port's belongs to its vehicle */
    memset(e, 0, sizeof *e);
    if (g_entity_count - i == 1) g_entity_count--;
}

Entity *Entity_Add(Node *n, void *a, const int32_t *b)
{
    Entity *e = NULL;
    for (int i = 0; i < g_entity_count; i++)
        if (!g_entities[i].node) {
            e = &g_entities[i];
            break;
        }
    if (!e) {
        if (g_entity_count == MAX_ENTITIES) {
            plat_log("You had exceeded the maximum number of entities allowed");   /* the original's fatal */
            return NULL;
        }
        e = &g_entities[g_entity_count++];
    }
    Node_Ref(n);
    e->node = n;
    e->a = NULL;
    if (a) {
        e->has_a = 1;
        e->a = a;
    }
    if (b) {
        memcpy(e->b, b, sizeof e->b);
        e->has_b = 1;
    }
    float min[3], max[3];
    Node_Bounds(n, min, max);
    for (int k = 0; k < 3; k++) e->c[k] = F32(MUL(ADD(max[k], min[k]), 0.5));   /* 0x4a5898 */
    e->r = Node_Radius(n, e->c[0], e->c[1], e->c[2]);
    if (n->flags & 0x10) g_user_entity = e;
    for (int i = 0; i < on_add_count; i++) on_add[i](e);
    return e;
}

bool Class_IsStructure(int cls) { return cls == 2 || cls == 3 || cls == 4 || cls == 0xb || cls == 0xc; }

Cell *List_Push(Cell *head, Cell **prev, void *value)
{
    Cell *c = malloc(sizeof *c);
    if (!c) return NULL;
    c->next = head;
    c->prev = (Cell *)prev;
    c->value = value;
    if (head) head->prev = c;
    if (prev) *prev = c;
    return c;
}

uint8_t *Data_New(size_t n)
{
    size_t al = (n + 7) & ~(size_t)7;
    uint8_t *p = calloc(1, 8 + al + al / 4 * sizeof(void *));
    if (!p) return NULL;
    memcpy(p, &(uint32_t){(uint32_t)n}, 4);
    return p + 8;
}

void **Data_Ptr(uint8_t *d, uint32_t off)
{
    uint32_t n;
    memcpy(&n, d - 8, 4);
    return (void **)(d + ((n + 7) & ~7u)) + off / 4;
}

void Data_Free(uint8_t *d)
{
    if (d) free(d - 8);
}

/* the class data allocator 0x41fb80 (its heap 0x4d3a3c; not cleared) */
static uint8_t *class_alloc(size_t n) { return Data_New(n); }
static void put32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
static void putf(uint8_t *p, float v) { memcpy(p, &v, 4); }
static float rdf(const uint8_t *p) { float v; memcpy(&v, p, 4); return v; }
static float getf(const uint8_t *p) { float v; memcpy(&v, p, 4); return v; }

/* structures 0x4306a0: 0x30 bytes, two words 400 (the strength, set from the record by 0x430e20) */
static uint8_t *structure_new(void)
{
    uint8_t *d = class_alloc(0x30);
    if (d) {
        put32(d, 400);
        put32(d + 4, 400);
    }
    return d;
}

/* class 10, 0x4345f0: 0x14 bytes {0, pi/4, 0, 400, 400} */
static uint8_t *class10_new(void)
{
    uint8_t *d = class_alloc(0x14);
    if (d) {
        put32(d + 4, 0x3f490fdb);
        put32(d + 0xc, 400);
        put32(d + 0x10, 400);
    }
    return d;
}

/* an identity frame (0x4db298) and two or three words (35, 36: 0x426350 / 0x426380, 41: 0x433340) */
static uint8_t *frame_new(uint32_t a)
{
    uint8_t *d = class_alloc(0x50);
    if (d) {
        memcpy(d, exe_data(0x4db298, 0x40), 0x40);
        put32(d + 0x40, a);
    }
    return d;
}
static uint8_t *class35_new(void) { return frame_new(0x14); }
static uint8_t *class36_new(void) { return frame_new(0x15); }
static uint8_t *class41_new(void) { return frame_new(0); }
static uint8_t *class31_new(void) { return class_alloc(8); }      /* 0x4342d0 */
static uint8_t *class32_new(void) { return class_alloc(0x10); }   /* 0x4337f0 (32, 33) */
static uint8_t *class40_new(void) { return class_alloc(0xb8); }   /* 0x433150, not cleared */
static uint8_t *class52_new(void) { return class_alloc(0x18); }   /* 0x4547f0, cleared */
static uint8_t *class20_new(void)                                 /* 0x434510 */
{
    uint8_t *d = class_alloc(8);
    if (d) put32(d, 400), put32(d + 4, 400);
    return d;
}
static uint8_t *class21_new(void)   /* 0x42f330 */
{
    static const uint32_t w[10] = {300, 300, 1, 0x3ccccccd, 0, 0x48927c00, 0x3f800000, 0x44834000, 0x45dac000, 0};
    uint8_t *d = class_alloc(0x2c);
    if (d) memcpy(d, w, sizeof w);
    return d;
}
static uint8_t *class22_new(void)   /* 0x4303b0 */
{
    static const uint32_t w[5] = {0, 150, 150, 0x3f800000, 0x3f800000};
    uint8_t *d = class_alloc(0x18);
    if (d) memcpy(d, w, sizeof w);
    return d;
}
static uint8_t *class23_new(void)   /* 0x430530 */
{
    static const uint32_t w[8] = {200, 200, 0, 0x3f800000, 0x3f800000, 0x3f800000, 0x3f800000, 0x3f800000};
    uint8_t *d = class_alloc(0x20);
    if (d) memcpy(d, w, sizeof w);
    return d;
}
static uint8_t *class24_new(void)   /* 0x42d850 */
{
    static const uint32_t w[5] = {0, 0, 10, 10, 0};
    uint8_t *d = class_alloc(0x18);
    if (d) memcpy(d, w, sizeof w);
    return d;
}

/* Body_Init 0x471680 */
void Body_Init(uint8_t *p)
{
    memset(p, 0, 0x30);
    put32(p + 4, 0x7149f2ca);
    put32(p + 0xc, 0x7149f2ca);
    put32(p + 0x10, 0x3f800000);
}

/* Motion_Init 0x469b80 */
void Motion_Init(uint8_t *p)
{
    put32(p, 0);
    put32(p + 4, 0);
    put32(p + 0x60, 0);
    put32(p + 0x64, 0);
    put32(p + 0x68, 0);
    Body_Init(p + 8);
}

/* 0x4716f0: the mass m and the inertia i, with 1 / m (0x4a66a4) and m / i (1e10 when i is not above
   1e-4, the bits compared as an integer) */
void Body_Set(uint8_t *p, float m, float i)
{
    uint32_t bm, bi;
    memcpy(&bm, &m, 4);
    memcpy(&bi, &i, 4);
    putf(p, m);
    if ((int32_t)bm > 0x38d1b717) putf(p + 4, F32(fpu_div(1.0, m)));
    putf(p + 0x10, i);
    putf(p + 0x14, (int32_t)bi > 0x38d1b717 ? F32(fpu_div(m, i)) : 1e10f);
}

/* the AI's skill tables by level (0x40b380, from 0x4088a0's set-up): 0x5ad0f0, 0x5ad110, 0x5ad130 and
   0x5ad150, five each */
static const float skill[4][5] = {
    {1.0f, 0.8f, 0.6f, 0.5f, 0.4f}, {1.0f, 0.8f, 0.6f, 0.5f, 0.4f},
    {1.0f, 0.9f, 0.8f, 0.7f, 0.6f}, {1.0f, 0.8f, 0.6f, 0.4f, 0.2f},
};

/* 0x40b430: the AI block's four skills */
static void ai_skills(uint8_t *ai, int a, int b, int c, int d)
{
    putf(ai + 0xa820, skill[0][a]);
    putf(ai + 0xa81c, skill[1][b]);
    putf(ai + 0xa824, skill[3][c]);
    putf(ai + 0xa828, skill[2][d]);
}

float AI_SkillAt(int table, int level)
{
    /* the tables lie 0x20 apart in the bss with zeros between (setSkill reads 0x5ad0ec + 4 x level) */
    return level >= 1 && level <= 5 ? skill[table][level - 1] : 0.0f;
}

/* 0x40fb20: a vehicle's AI block (0xa9d8 bytes from the game heap 0x44e380, not cleared) */
static uint8_t *ai_new(void)
{
    uint8_t *a = Data_New(0xa9d8);
    if (!a) return NULL;
    static const uint16_t zero[] = {8, 0x98, 0x7c, 0x88, 0x94, 0x90, 0x9c};
    for (size_t i = 0; i < sizeof zero / sizeof zero[0]; i++) put32(a + zero[i], 0);
    put32(a + 0x9cf0, 0);
    put32(a + 0x9cf4, 0);
    put32(a + 0x84, 2);
    *Data_Ptr(a, 0x60) = a + 100;
    put32(a + 0x5c, 1);
    put32(a + 0xa818, 4);
    ai_skills(a, 4, 4, 4, 4);
    put32(a + 0xa6d0, 1);
    put32(a + 0x9d14, 1);
    for (int i = 0; i < 4; i++) put32(a + 0x9d58 + 4 * i, 0x3f800000);
    for (int i = 0; i < 3; i++) put32(a + 0xa6d4 + 4 * i, 0xfffffff6);
    for (uint32_t o = 0xa6e0; o <= 0xa6f0; o += 4) put32(a + o, 0);
    put32(a + 0xa954, 1);
    put32(a + 0xa96c, 0);
    for (uint32_t o = 0xa998; o <= 0xa9c8; o += 4) put32(a + o, 0);
    put32(a + 0xa9a8, 1);
    put32(a + 0xa9cc, 0x7f7fffff);
    a[0xa9d0] = 0;
    put32(a + 0xa9d4, 0);
    put32(a + 0xa714, 1);
    return a;
}

/* 0x429210: a vehicle's data (0x50c bytes, cleared) */
static uint8_t *vehicle_data(void)
{
    uint8_t *d = class_alloc(VEH_SIZE);
    if (!d) return NULL;
    Motion_Init(d + 0x38);
    static const uint32_t k[13] = {0xbf800000, 0x3f800000, 0xc0000000, 0x3f800000, 0x3f800000, 0xc0000000, 0x3f800000,
                                   0x3f800000, 0x40000000, 0xbf800000, 0x3f800000, 0x40000000, 0x3fc00000};
    memcpy(d + 4, k, sizeof k);
    Body_Init(d + 0xa4);
    put32(d + 0xe0, 0);
    put32(d + 0xe4, 0);
    put32(d + 0xe8, 0x3f800000);
    for (uint32_t o = 0xec; o <= 0x100; o += 4) put32(d + o, 0);
    put32(d + 0x104, 3);
    *Data_Ptr(d, 0x108) = ai_new();
    put32(d + 0x10c, 0);
    put32(d + 0x110, 0x3e4ccccd);
    put32(d + 0x114, 0x3faf5c29);
    put32(d + 0x118, 0x402f5c29);
    put32(d + 0x120, 0);
    put32(d + 0x124, 0);
    Body_Set(d + 0xa4, 1500.0f, 1500.0f);   /* 0x44bb8000 */
    put32(d + 0x12c, 0);
    put32(d + 0x130, 0);
    put32(d + 0x134, 0);
    put32(d + 0x128, 0x3f800000);
    for (int i = 0; i < 4; i++) {
        put32(d + 0x138 + 4 * i, 400);
        put32(d + 0x158 + 4 * i, 400);
        put32(d + 0x148 + 4 * i, 400);
        put32(d + 0x168 + 4 * i, 400);
    }
    for (int i = 0; i < 5; i++) {
        put32(d + 0x18c + 4 * i, 400);
        put32(d + 0x178 + 4 * i, 400);
    }
    put32(d + 0x19c, 100);
    put32(d + 0x188, 100);
    /* the rest of 0x1a0-0x480 the clearing already zeroed */
    put32(d + 0x454, Light_IsSun() == 0 ? 0x40 : 0);
    put32(d + 0x45c, 2);
    put32(d + 0x464, 0x3f800000);
    put32(d + 0x46c, 0x411ccccd);
    put32(d + 0x478, 0x3dcccccd);
    return d;
}

/* class 1, 0x429170: the data and its four parts, each a node of its own (Node_Create without a parent:
   each becomes the root 0x50a394) */
static uint8_t *class1_new(void)
{
    uint8_t *d = vehicle_data();
    if (!d) return NULL;
    static const struct { uint16_t off; int cls; } parts[] = {{0x3c4, 0x15}, {0x3cc, 0x16}, {0x3c8, 0x17}, {0x3c0, 0x14}};
    for (int i = 0; i < 4; i++) {
        Node *n = Node_Create(NULL);
        *Data_Ptr(d, parts[i].off) = n;
        Class_Set(n, parts[i].cls);
    }
    return d;
}

/* class 9, 0x431390: and a block of 0x18 (not cleared) at +0x508 */
static uint8_t *class9_new(void)
{
    uint8_t *d = class1_new();
    uint8_t *b = Data_New(0x18);
    *Data_Ptr(d, 0x508) = b;
    put32(b + 0x14, 0);
    put32(b + 0xc, 0);
    put32(b + 0x10, 0);
    put32(b + 4, 600);
    put32(b, 600);
    putf(b + 8, F32(fpu_div(1.0, 600.0)));   /* 0x4a5d1c / (float)600 */
    put32(d + 0x454, 0x81);
    return d;
}

/* class 8, 0x4288c0: and a block of 0x54 (not cleared) at +0x508 */
static uint8_t *class8_new(void)
{
    uint8_t *d = class1_new();
    uint8_t *b = Data_New(0x54);
    *Data_Ptr(d, 0x508) = b;
    put32(b + 4, 600);
    put32(b, 600);
    putf(b + 8, F32(fpu_div(1.0, 600.0)));   /* 0x4a598c */
    put32(b + 0x4c, 1);
    put32(b + 0x50, 5);
    return d;
}

/* class 7, 0x432ad0: 0xa0 bytes (not cleared) */
static uint8_t *class7_new(void)
{
    uint8_t *d = class_alloc(0xa0);
    if (!d) return NULL;
    Motion_Init(d);
    for (uint32_t o = 0x6c; o <= 0x8c; o += 4) put32(d + o, 0);
    put32(d + 0x94, 0x3f800000);
    put32(d + 0x90, 1);
    put32(d + 0x98, 400);
    put32(d + 0x9c, 400);
    return d;
}

/* 0x453df0: a two-pole filter of damping a and period p (at least 4, the bits compared as an integer):
   its coefficients and the gain that makes its response to a unit step 1, from 200 steps */
void Filter_Init(uint8_t *f, float a, float p)
{
    uint32_t bp;
    memcpy(&bp, &p, 4);
    if ((int32_t)bp < 0x40800000) p = 4.0f;
    double c = fpu_mul(cos(fpu_div(6.2831854820251465, p)), a);   /* 0x4a6170 */
    double k0 = fpu_mul(c, -2.0), k1 = fpu_mul(a, a);   /* 0x4a6174 */
    double y1 = 0, y2 = 0;
    for (int i = 0; i < 200; i++) {
        double y = fpu_sub(fpu_sub(1.0, fpu_mul(k0, y1)), fpu_mul(k1, y2));
        y2 = y1;
        y1 = y;
    }
    double g = fpu_div(1.0, y1), z = 0;
    memcpy(f, &k0, 8);
    memcpy(f + 8, &k1, 8);
    memcpy(f + 0x10, &g, 8);
    memcpy(f + 0x18, &z, 8);
    memcpy(f + 0x20, &z, 8);
}

double Filter_Step(uint8_t *f, float x)
{
    double k0, k1, g, y1;
    memcpy(&k0, f, 8);
    memcpy(&k1, f + 8, 8);
    memcpy(&g, f + 0x10, 8);
    memcpy(&y1, f + 0x18, 8);
    double y2;
    memcpy(&y2, f + 0x20, 8);
    double y = fpu_sub(fpu_sub(fpu_mul(g, x), fpu_mul(k1, y2)), fpu_mul(y1, k0));
    memcpy(f + 0x20, f + 0x18, 8);
    memcpy(f + 0x18, &y, 8);
    return y;
}

/* class 30 (a wheel), 0x42faf0: 0x88 bytes */
static uint8_t *class30_new(void)
{
    uint8_t *d = class_alloc(0x88);
    if (!d) return NULL;
    static const uint32_t w[14] = {0, 100, 100, 0x3f800000, 0x3f800000, 0x3f800000, 0x3f800000, 0x3f1100e7,
                                   0, 0, 0, 0x42800000, 0x42800000, 0};
    memcpy(d, w, sizeof w);
    float r = getf(d + 0x1c);
    putf(d + 0x38, F32(fpu_mul(r, 0.25)));    /* 0x4a5cb8 */
    putf(d + 0x3c, F32(fpu_mul(r, -0.25)));   /* 0x4a5cbc */
    memset(d + 0x40, 0, 0x18);
    Filter_Init(d + 0x58, 0.7f, 15.0f);   /* 0x3f333333, 0x41700000 */
    put32(d + 0x80, 0x40400000);
    put32(d + 0x84, 0);
    return d;
}

static uint8_t *debris_new(void);
static void debris_free(uint8_t *d);

typedef uint8_t *(*ClassNew)(void);
/* the table 0x4d3c20 in its order: the class and its constructor (handler 0) where ported. Entry 0
   (class 0) is never found. (50, 51 and 5 have no constructor.) Where the
   original leaves the data uncleared, the port's is zero. */
static const struct {
    int32_t cls;
    ClassNew make;
} classes[] = {
    {0, NULL},  {2, structure_new}, {4, structure_new}, {1, class1_new},  {9, class9_new},  {8, class8_new},
    {7, class7_new}, {10, class10_new}, {21, class21_new}, {22, class22_new}, {30, class30_new}, {23, class23_new}, {20, class20_new},
    {35, class35_new}, {36, class36_new}, {34, Radar_New}, {52, class52_new}, {31, class31_new}, {32, class32_new},
    {33, class32_new}, {53, debris_new}, {50, NULL}, {51, NULL}, {5, NULL}, {24, class24_new}, {41, class41_new},
    {40, class40_new},
};
#define NCLASSES (int)(sizeof classes / sizeof classes[0])

const int32_t *Class_Table(int *n)
{
    static int32_t t[NCLASSES];
    for (int i = 0; i < NCLASSES; i++) t[i] = classes[i].cls;
    *n = NCLASSES;
    return t;
}

static int class_index(int cls)
{
    if (Class_IsStructure(cls) && cls != 4) cls = 2;
    for (int i = 0; i < NCLASSES; i++)
        if (classes[i].cls == cls) return i;
    return 0;
}

void Body_State(uint8_t out[0x3c], const Node *n)
{
    memset(out, 0, 0x3c);   /* the original's 0x30-0x3c are its stack's when the body is a default one */
    switch (classes[class_index(n->cls)].cls) {
    case 1: case 8: case 9:   /* 0x42a780, 0x428c90, 0x431d90 */
        memcpy(out, n->data + 0xa4, 0x3c);
        break;
    case 10:   /* 0x4347f0: still, turning at the data's first three words */
        Body_Init(out);
        memcpy(out + 0x24, n->data, 12);
        break;
    case 51:   /* Shot_Body 0x459810: a shot's, mass 15, its velocity (record +0x14) turned to the world with that
                  velocity's length, its spin (record +0x2c); +4 and +0xc are the original's stack's */
        if (!n->data) {
            Body_Init(out);
            break;
        } else {
            const float *m = n->frame.m, *v = (const float *)(n->data + 0x14);
            float w[3];
            w[0] = F32(ADD(ADD(MUL(m[0], v[0]), MUL(m[3], v[1])), MUL(m[6], v[2])));
            w[1] = F32(ADD(ADD(MUL(m[4], v[1]), MUL(m[1], v[0])), MUL(m[7], v[2])));
            w[2] = F32(ADD(ADD(MUL(m[8], v[2]), MUL(m[5], v[1])), MUL(m[2], v[0])));
            putf(out, 15.0f);
            putf(out + 8, F32(fpu_sqrt(ADD(ADD(MUL(w[2], w[2]), MUL(w[0], w[0])), MUL(w[1], w[1])))));
            memcpy(out + 0x18, w, 12);
            memcpy(out + 0x24, n->data + 0x2c, 12);
        }
        break;
    default:   /* none, and 7's 0x433050 */
        Body_Init(out);
    }
}

void Class_Set(Node *n, int cls)
{
    n->cls = cls;
    int i = class_index(cls);
    if (i > 0) {
        if (classes[i].make) n->data = classes[i].make();
        Node_Ref(n);
        g_class_nodes = List_Push(g_class_nodes, NULL, n);
        g_class_cells = List_Push(g_class_cells, NULL, g_class_nodes);
        g_class_count[0]++;
        g_class_count[1]++;
    }
}

void List_Remove(Cell **head, Cell *c)
{
    if (c->next) c->next->prev = c->prev;
    *(c->prev ? &c->prev->next : head) = c->next;
    free(c);
}

void Class_Unlist(Node *n)
{
    Node *live = Node_Live(n);
    if (live) {
        if (live->data) Class_Free(live->cls, live->data);
        live->cls = 0;
        live->data = NULL;
    }
    Cell *c = g_class_nodes;
    while (c && c->value != n) c = c->next;
    if (!c) return;
    if (live) Node_Unref(live);
    for (Cell *k = g_class_cells; k; k = k->next)
        if (k->value == c) {
            List_Remove(&g_class_cells, k);
            g_class_count[0]--;
            break;
        }
    List_Remove(&g_class_nodes, c);
    g_class_count[1]--;
}

void Class_Free(int cls, uint8_t *data)
{
    /* handler 11 of the class's entry; the ported constructors' data is the heap's (0x430e10 for 2 and
       4, 0x4346a0 for 10) */
    if (data && cls == 53) debris_free(data);   /* 0x45bfd0: back to the pool */
    else if (data && (cls == 32 || cls == 33)) {
        Window_Free(data);   /* 0x433940 */
        Data_Free(data);
    }
    else if (data && classes[class_index(cls)].make) Data_Free(data);
}

/* the name table: {name, node} entries; the original's grows by 0x100 after its first 0x800, through a
   reallocation whose success it takes for failure (0x492b00): the port's never fills */
typedef struct {
    char name[8];
    Node *node;
} NameEntry;
static NameEntry *names;
static int names_count, names_cap;

static void names_free(void)   /* 0x492c10 */
{
    free(names);
    names = NULL;
    names_cap = names_count = 0;
}

bool Names_Add(const char *name, Node *n)
{
    if (!name) {   /* 0x492ac0 */
        free(names);
        names_cap = 0x800;
        names_count = 0;
        names = calloc((size_t)names_cap, sizeof *names);
        return names != NULL;
    }
    if (names_count >= names_cap) return false;
    /* the original warns of a duplicate (outside network games); the entry is added anyway */
    memcpy(names[names_count].name, name, 8);
    names[names_count].node = n;
    names_count++;
    return true;
}

Node *Names_Find(const char *name)
{
    for (int i = names_count - 1; i >= 0; i--)
        if (memcmp(names[i].name, name, 8) == 0) return names[i].node;
    return NULL;
}

int Names_Count(void) { return names_count; }

/* the chunk table: a record (0x470 bytes) per node */
typedef struct ChunkRec {
    Node *node;
    uint16_t count;
    Chunk chunk[10];
    struct ChunkRec *next;
    char names[13][16];   /* 0x45afa0's, when set (the record grows to 0x540 bytes) */
    bool has_names;
} ChunkRec;
#define CHUNK_BUCKETS 0x71
static ChunkRec *chunk_table[CHUNK_BUCKETS];   /* 0x570aa8 */
Node *g_chunk_mgr;                             /* 0x4dc428 */

static unsigned chunk_bucket(const Node *n) { return (unsigned)(((uintptr_t)n * 0x6cd + 0xaab) % CHUNK_BUCKETS); }

/* the debris (class 53): 150 data blocks of 0x44 bytes (0x570c78), free through their +0x40; the oldest
   piece thrown (0x4dc42c) and its data (0x4dc430) give theirs up when none is left */
#define DEBRIS_POOL 150
static uint8_t *debris_pool[DEBRIS_POOL];
static uint8_t *debris_head;     /* 0x570c6c */
static int32_t debris_closed;    /* 0x570c70: the table torn down (0x45bba0) */
Node *g_debris_oldest;           /* 0x4dc42c */
static uint8_t *debris_oldest_d; /* 0x4dc430 */

/* the noise (0x573490): 251 sums of ten rand()s, scaled to about -1..1, read in turn (0x573488) */
static float noise[251];
static int noise_at;

static void noise_init(void)   /* 0x461750 */
{
    uint32_t keep = crt_seed();
    crt_srand(0);
    for (int i = 0; i < 251; i++) {
        float v = 0;
        double s = 0;
        for (int k = 0; k < 10; k++) {
            s = fpu_add(fpu_mul((double)crt_rand(), 3.0518509447574615e-05f), v);   /* 0x4a63b0 */
            v = fpu_f32(s);
        }
        noise[i] = fpu_f32(fpu_sub(fpu_mul(s, 0.2f), 1.0f));   /* 0x4a63b4, 0x4a63b8 */
    }
    noise_at = 0;
    crt_srand(keep);   /* the original reseeds from the clock (time(NULL)); the port never does (crt.h) */
}

static float noise_next(void)
{
    float v = noise[noise_at];
    noise_at = (noise_at + 1) % 251;
    return v;
}

void Debris_Throw(Node *c, const float v[3])
{
    uint8_t *d = c->data;
    float w[3];
    for (int i = 0; i < 3; i++) {
        w[i] = fpu_f32(fpu_add(fpu_mul(noise_next(), 10.0f), v[i]));   /* 0x4a63bc */
        putf(d + 0x18 + 4 * i, w[i]);
    }
    putf(d + 8, fpu_f32(fpu_sqrt(fpu_add(fpu_add(fpu_mul(w[2], w[2]), fpu_mul(w[0], w[0])), fpu_mul(w[1], w[1])))));
    for (int i = 0; i < 3; i++) putf(d + 0x24 + 4 * i, fpu_f32(fpu_mul(noise_next(), 20.0f)));   /* 0x4a63c0 */
}

static uint8_t *debris_new(void)   /* 0x45bc10 */
{
    if (!debris_head) {
        Node *old = g_debris_oldest;
        if (!old) return NULL;
        Class_Unlist(old);
        Node_Free(old);
        if (!debris_head) return NULL;
    }
    uint8_t *d = debris_head;
    debris_head = *(uint8_t **)Data_Ptr(d, 0x40);
    *Data_Ptr(d, 0x40) = NULL;
    Body_Init(d);
    putf(d + 0x3c, fpu_f32(fpu_add(g_clock_time, 15.0f)));   /* 0x4a6288: gone in 15 s */
    return d;
}

static void debris_free(uint8_t *d)   /* 0x45bfd0 */
{
    if (debris_closed) return;
    if (d == debris_oldest_d) {   /* the oldest goes: the one thrown after it (before it in the list) */
        g_debris_oldest = g_debris_oldest->prev;
        debris_oldest_d = g_debris_oldest->data;
        if (g_debris_oldest == g_chunk_mgr) g_debris_oldest = NULL, debris_oldest_d = NULL;
    }
    *Data_Ptr(d, 0x40) = debris_head;
    debris_head = d;
}

void Debris_First(Node *c)
{
    if (!g_debris_oldest) {
        g_debris_oldest = c;
        debris_oldest_d = c->data;
    }
}

/* Debris_Frame 0x45bca0 (class 53's first per-frame handler): past its time, or off the grid, the piece is
   gone. Otherwise its centre falls (-9.8) and it turns by its spin; on the ground it bounces: its upward
   speed the mean of the step's two (gone when above 0), its velocity reflected off the ground's normal
   and quartered, all of it and the spin stopped under 0.1 up or down; the frame put back round the centre
   and the speed kept */
void Debris_Frame(Node *n)
{
    uint8_t *d = n->data;
    float dt = g_clock_step;   /* 0x450f30 */
    if (g_clock_time > rdf(d + 0x3c)) {   /* test 0x41: only above */
        Class_Unlist(n);
        Node_Free(n);   /* tagged "chnkmgr" */
        return;
    }
    float p[3], v[3];
    Frame_Points((float (*)[3])p, (const float (*)[3])n->centre, 1, &n->frame);
    for (int i = 0; i < 3; i++) v[i] = rdf(d + 0x18 + 4 * i);
    float vy0 = v[1];
    Ballistic(p, v, -9.8f, dt);   /* 0xc11ccccd */
    if (!Terrain_Inside(p[0], p[2])) {
        Class_Unlist(n);
        Node_Free(n);
        return;
    }
    Frame t;
    Frame_Turn(&t, &n->frame, rdf(d + 0x24), rdf(d + 0x28), rdf(d + 0x2c), dt);
    n->frame = t;
    float h, nr[3];
    Ground_Height(p[0], p[1], p[2], &h, nr);
    if (!(p[1] > h)) {
        v[1] = fpu_f32(fpu_mul(fpu_add(vy0, v[1]), exe_f32(0x4a6280)));
        if (v[1] > exe_f32(0x4a6278)) {
            Class_Unlist(n);
            Node_Free(n);
            return;
        }
        double k = fpu_mul(fpu_add(fpu_add(fpu_mul(nr[1], v[1]), fpu_mul(nr[2], v[2])), fpu_mul(nr[0], v[0])), exe_f32(0x4a6290));
        p[1] = h;
        float a[3] = {fpu_f32(fpu_add(fpu_mul(nr[0], k), v[0])), fpu_f32(fpu_add(fpu_mul(nr[1], k), v[1])),
                      fpu_f32(fpu_add(fpu_mul(k, nr[2]), v[2]))};
        for (int i = 0; i < 3; i++) v[i] = fpu_f32(fpu_mul(a[i], exe_f32(0x4a6294)));
        double still = 0;   /* 0x4a6298 (a double) */
        const uint8_t *q = exe_data(0x4a6298, 8);
        if (q) memcpy(&still, q, 8);
        if (fabs(v[1]) < still || v[1] != v[1]) {
            v[0] = v[1] = v[2] = 0;
            putf(d + 0x24, 0), putf(d + 0x28, 0), putf(d + 0x2c, 0);
        }
    }
    const float *m = n->frame.m, *c = n->centre;
    n->frame.pos[0] = fpu_sub(p[0], fpu_add(fpu_add(fpu_mul(m[6], c[2]), fpu_mul(m[3], c[1])), fpu_mul(m[0], c[0])));
    n->frame.pos[1] = fpu_sub(p[1], fpu_add(fpu_add(fpu_mul(m[7], c[2]), fpu_mul(m[4], c[1])), fpu_mul(m[1], c[0])));
    n->frame.pos[2] = fpu_sub(p[2], fpu_add(fpu_add(fpu_mul(m[2], c[0]), fpu_mul(m[8], c[2])), fpu_mul(m[5], c[1])));
    for (int i = 0; i < 3; i++) putf(d + 0x18 + 4 * i, v[i]);
    putf(d + 8, fpu_f32(fpu_sqrt(fpu_add(fpu_add(fpu_mul(v[1], v[1]), fpu_mul(v[2], v[2])), fpu_mul(v[0], v[0])))));
}

/* the particles' templates (0x45c190's table 0x573450, 13 buckets by the upper-cased name; the port keeps a
   list, the lookups the same): CHUNK1, CHUNK2, CHUNK4, CHUNK3, each a node of its .geo, life 5 */
typedef struct { uint8_t key[8]; Node *node; float life; } ParticleTpl;
static ParticleTpl particle_tpl[4];
static int particle_tpls;

static void key_upper(uint8_t out[8], const uint8_t name[8])
{
    for (int i = 0; i < 8; i++) out[i] = (uint8_t)(name[i] >= 'a' && name[i] <= 'z' ? name[i] - 32 : name[i]);   /* 0x497870 */
}

static ParticleTpl *particle_find(const uint8_t name[8])
{
    uint8_t k[8];
    key_upper(k, name);
    for (int i = 0; i < particle_tpls; i++)
        if (!memcmp(particle_tpl[i].key, k, 8)) return &particle_tpl[i];
    return NULL;
}

void Particles_Init(void)
{
    static const uint32_t names[4] = {0x4d5394, 0x4d538c, 0x4d57c4, 0x4d5ce0};
    particle_tpls = 0;
    for (int i = 0; i < 4; i++) {
        uint8_t name[8];
        const uint8_t *p = exe_data(names[i], 8);
        if (!p) continue;
        memcpy(name, p, 8);
        Node *n = Node_New(NULL, (const char *)name);   /* 0x484850 */
        n->flags = 0;
        n->radius = 1.0f;
        n->geom = NULL;
        Lod_Add(n, Obj_Name(name, exe_str(0x4dc458)), 0, 0);   /* ".geo" */
        if (particle_find(name)) continue;   /* (the original's name already there: kept) */
        ParticleTpl *t = &particle_tpl[particle_tpls++];
        key_upper(t->key, name);
        t->node = n;
        t->life = 5.0f;
    }
}

int Debris_Start(Node *n, const float *v, float life)
{
    if (!n) return 0;
    Class_Set(n, 53);   /* 0x425700 */
    if (!n->data) {
        Node_Free(n);   /* tagged "chnkmgr" */
        return 0;
    }
    Node_Attach(n, g_chunk_mgr);   /* 0x41fa20 */
    Debris_First(n);
    putf(n->data + 0x3c, fpu_f32(fpu_add(g_clock_time, life)));
    if (v) Debris_Throw(n, v);
    Class_StartAll();   /* 0x425920 */
    return 1;
}

/* Node_CopyTree 0x45c100: n copied under parent (NULL: none; tagged "chnkmgr"): its frame, its hidden bit,
   no mesh, class or data; its levels of detail and textures; its children likewise; n freed */
static Node *copy_tree(Node *n, Node *parent)
{
    if (!n) return NULL;
    Node *c = Node_Create(parent);
    c->frame = n->frame;
    c->flags = n->flags & 1;
    c->geom = NULL;
    c->cls = 0;
    c->data = NULL;
    Lod_Copy(n, c);                               /* 0x47d850 */
    Tex_CopyOwner((uintptr_t)n, (uintptr_t)c);    /* 0x47bf10 */
    for (Node *k = n->child, *next; k; k = next) {
        next = k->next;
        copy_tree(k, c);
    }
    Node_Free(n);
    return c;
}

int Part_Throw(Node *n, const float *v, int own_box, const float *box)
{
    if (!n) return 0;
    Node *t;
    if (!own_box) {   /* under a node at its place, its centre and radius from the meshes */
        t = Node_Create(NULL);
        Node_FrameIn(&t->frame, n, NULL);
        t->flags = n->flags & 1;
        t->geom = NULL;
        Node_Detach(n);
        memcpy(&n->frame, exe_data(0x4db298, sizeof n->frame), sizeof n->frame);
        copy_tree(n, t);
        float lo[3], hi[3];
        Node_Bounds(t->child, lo, hi);
        for (int i = 0; i < 3; i++) t->centre[i] = fpu_f32(fpu_mul(fpu_add(hi[i], lo[i]), exe_f32(0x4a6280)));
        t->radius = Node_Radius(t->child, t->centre[0], t->centre[1], t->centre[2]);
    } else {   /* itself at its place, with the box given (an entity's centre and radius) */
        Frame f;
        Node_FrameIn(&f, n, NULL);
        n->frame = f;
        Node_Detach(n);
        t = copy_tree(n, NULL);
        if (!t) return 0;
        memcpy(t->centre, box, 12);
        t->radius = box[3];
    }
    int32_t rb;
    memcpy(&rb, &t->radius, 4);
    if (rb < 0x38d1b717) {   /* about 1e-4 */
        Node_Free(t);
        return 0;
    }
    Class_Set(t, 0x35);   /* 0x425700 */
    if (!t->data) {
        Node_Free(t);
        return 0;
    }
    Node_Attach(t, g_chunk_mgr);
    Debris_First(t);
    if (v) Debris_Throw(t, v);
    Class_StartAll();   /* 0x425920 */
    return 1;
}

Node *Particle_New(const uint8_t name[8], float x, float y, float z, const float *v, float life)
{
    Node *n = Node_Create(NULL);   /* 0x41f320, tagged "chnkmgr" */
    n->frame.pos[0] = x, n->frame.pos[1] = y, n->frame.pos[2] = z;
    n->flags = 0;
    n->geom = NULL;
    ParticleTpl *t = particle_find(name);
    if (t) Lod_Copy(t->node, n);   /* 0x47d850 */
    Debris_Start(n, v, life);
    return n;   /* (freed already when it got no data, as the original's) */
}

void Chunks_Init(void)
{
    noise_init();
    for (int i = 0; i < CHUNK_BUCKETS; i++)
        for (ChunkRec *r = chunk_table[i], *nx; r; r = nx) {
            nx = r->next;
            free(r);
        }
    memset(chunk_table, 0, sizeof chunk_table);
    g_chunk_mgr = Node_Create(NULL);
    g_chunk_mgr->data = NULL;
    g_chunk_mgr->flags = 0x100;
    Entity_Add(g_chunk_mgr, NULL, NULL);
    for (int i = 0; i < DEBRIS_POOL; i++) {
        if (!debris_pool[i]) debris_pool[i] = Data_New(0x44);
        else memset(debris_pool[i], 0, 0x44);
    }
    for (int i = 0; i < DEBRIS_POOL; i++) *Data_Ptr(debris_pool[i], 0x40) = i + 1 < DEBRIS_POOL ? debris_pool[i + 1] : NULL;
    debris_head = debris_pool[0];
    g_debris_oldest = NULL;
    debris_oldest_d = NULL;
    debris_closed = 0;
}

static ChunkRec *chunk_find(const Node *n)
{
    for (ChunkRec *r = chunk_table[chunk_bucket(n)]; r; r = r->next)
        if (r->node == n) return r;
    return NULL;
}

bool Chunk_Add(Node *n, int32_t a, int32_t b, const Frame *f, const int32_t p[4], const float box[6])
{
    ChunkRec *r = chunk_find(n);
    if (!r) {
        r = calloc(1, sizeof *r);
        if (!r) return false;
        r->node = n;
        unsigned k = chunk_bucket(n);
        r->next = chunk_table[k];
        chunk_table[k] = r;
    }
    if (r->count >= 10) return false;
    Chunk *c = &r->chunk[r->count++];
    c->a = a;
    c->b = b;
    c->frame = *f;
    memcpy(c->p, p, sizeof c->p);
    memcpy(c->box, box, sizeof c->box);
    return true;
}

bool Chunk_SetNames(const Node *n, const char *names)
{
    unsigned k = chunk_bucket(n);
    ChunkRec **pp = &chunk_table[k];
    while (*pp && (*pp)->node != n) pp = &(*pp)->next;
    ChunkRec *r = *pp;
    if (!r) return false;
    /* off its bucket's list and back at its head (the reallocation's) */
    *pp = r->next;
    r->next = chunk_table[k];
    chunk_table[k] = r;
    for (int i = 0; i < 13; i++) strncpy(r->names[i], names + 13 * i, 16);
    r->has_names = true;
    return true;
}

const char *Chunk_Name(const Node *n, int i)
{
    const ChunkRec *r = chunk_find(n);
    return r && r->has_names && i >= 0 && i < 13 ? r->names[i] : NULL;
}

void Chunk_Drop(const Node *n)
{
    unsigned k = chunk_bucket(n);
    for (ChunkRec **pp = &chunk_table[k]; *pp; pp = &(*pp)->next)
        if ((*pp)->node == n) {
            ChunkRec *r = *pp;
            *pp = r->next;
            free(r);
            return;
        }
}

void Chunk_Remove(const Node *n, int i)
{
    ChunkRec *r = chunk_find(n);
    if (!r || i < 0 || i >= r->count) return;
    memmove(&r->chunk[i], &r->chunk[i + 1], (size_t)(r->count - i - 1) * sizeof r->chunk[0]);
    if (--r->count == 0) Chunk_Drop(n);
}

const Chunk *Chunk_List(const Node *n, int *count)
{
    const ChunkRec *r = chunk_find(n);
    *count = r ? r->count : 0;
    return r ? r->chunk : NULL;
}
