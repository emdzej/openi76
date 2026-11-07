/* Entities and classes (entity.h). In the current rounding mode (render/fpu.h). */
#include "entity.h"
#include "exe.h"
#include "platform.h"
#include "render/fpu.h"
#include "render/light.h"
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

Cell *g_class_nodes, *g_class_cells;
int g_class_count[2];

void Entity_Init(void)
{
    memset(g_entities, 0, sizeof g_entities);
    g_entity_count = 0;
    g_user_entity = NULL;
    memset(on_add, 0, sizeof on_add);
    on_add_count = 0;
    /* the removal callbacks (0x54f6d8) and the ids (0x547a58) are not ported yet */
    Names_Add(NULL, NULL);   /* 0x492ac0: a new table */
}

void Entity_OnAdd(EntityFn fn) { on_add[on_add_count++] = fn; }

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

/* 0x471680: a body's state: mass and inertia 1e30 (0x7149f2ca) with no inverse, one 1.0 */
static void body_init(uint8_t *p)
{
    memset(p, 0, 0x30);
    put32(p + 4, 0x7149f2ca);
    put32(p + 0xc, 0x7149f2ca);
    put32(p + 0x10, 0x3f800000);
}

/* 0x469b80 */
static void motion_init(uint8_t *p)
{
    put32(p, 0);
    put32(p + 4, 0);
    put32(p + 0x60, 0);
    put32(p + 0x64, 0);
    put32(p + 0x68, 0);
    body_init(p + 8);
}

/* 0x4716f0: the mass m and the inertia i, with 1 / m (0x4a66a4) and m / i (1e10 when i is not above
   1e-4, the bits compared as an integer) */
static void body_set(uint8_t *p, float m, float i)
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
    motion_init(d + 0x38);
    static const uint32_t k[13] = {0xbf800000, 0x3f800000, 0xc0000000, 0x3f800000, 0x3f800000, 0xc0000000, 0x3f800000,
                                   0x3f800000, 0x40000000, 0xbf800000, 0x3f800000, 0x40000000, 0x3fc00000};
    memcpy(d + 4, k, sizeof k);
    body_init(d + 0xa4);
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
    body_set(d + 0xa4, 1500.0f, 1500.0f);   /* 0x44bb8000 */
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
    motion_init(d);
    for (uint32_t o = 0x6c; o <= 0x8c; o += 4) put32(d + o, 0);
    put32(d + 0x94, 0x3f800000);
    put32(d + 0x90, 1);
    put32(d + 0x98, 400);
    put32(d + 0x9c, 400);
    return d;
}

/* 0x453df0: a two-pole filter of damping a and period p (at least 4, the bits compared as an integer):
   its coefficients and the gain that makes its response to a unit step 1, from 200 steps */
static void filter_init(uint8_t *f, float a, float p)
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
    filter_init(d + 0x58, 0.7f, 15.0f);   /* 0x3f333333, 0x41700000 */
    put32(d + 0x80, 0x40400000);
    put32(d + 0x84, 0);
    return d;
}

typedef uint8_t *(*ClassNew)(void);
/* the table 0x4d3c20 in its order: the class and its constructor (handler 0) where ported. Entry 0
   (class 0) is never found. The constructors still missing: 34 0x41ff50 and 53 0x45bc10 (50, 51 and 5
   have none). Where the
   original leaves the data uncleared, the port's is zero. */
static const struct {
    int32_t cls;
    ClassNew make;
} classes[] = {
    {0, NULL},  {2, structure_new}, {4, structure_new}, {1, class1_new},  {9, class9_new},  {8, class8_new},
    {7, class7_new}, {10, class10_new}, {21, class21_new}, {22, class22_new}, {30, class30_new}, {23, class23_new}, {20, class20_new},
    {35, class35_new}, {36, class36_new}, {34, NULL}, {52, class52_new}, {31, class31_new}, {32, class32_new},
    {33, class32_new}, {53, NULL}, {50, NULL}, {51, NULL}, {5, NULL}, {24, class24_new}, {41, class41_new},
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

void Class_Free(int cls, uint8_t *data)
{
    /* handler 11 of the class's entry; the ported constructors' data is the heap's (0x430e10 for 2 and
       4, 0x4346a0 for 10) */
    if (data && classes[class_index(cls)].make) Data_Free(data);
}

/* the name table: {name, node} entries; the original's grows by 0x100 after its first 0x800, through a
   reallocation whose success it takes for failure (0x492b00): the port's never fills */
typedef struct {
    char name[8];
    Node *node;
} NameEntry;
static NameEntry *names;
static int names_count, names_cap;

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

void Chunks_Init(void)
{
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

const Chunk *Chunk_List(const Node *n, int *count)
{
    const ChunkRec *r = chunk_find(n);
    *count = r ? r->count : 0;
    return r ? r->chunk : NULL;
}
