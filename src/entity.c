/* Entities and classes (entity.h). In the current rounding mode (render/fpu.h). */
#include "entity.h"
#include "exe.h"
#include "platform.h"
#include "render/fpu.h"
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

/* the class data allocator 0x41fb80 (its heap 0x4d3a3c; not cleared) */
static uint8_t *class_alloc(size_t n) { return calloc(1, n); }
static void put32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }

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

typedef uint8_t *(*ClassNew)(void);
/* the table 0x4d3c20 in its order: the class and its constructor (handler 0) where ported. Entry 0
   (class 0) is never found. The constructors still missing: 1 0x429170, 9 0x431390, 8 0x4288c0, 7
   0x432ad0, 21 0x42f330, 22 0x4303b0, 30 0x42faf0, 23 0x430530, 20 0x434510, 35 0x426350, 36 0x426380,
   34 0x41ff50, 52 0x4547f0, 31 0x4342d0, 32 and 33 0x4337f0, 53 0x45bc10, 24 0x42d850, 41 0x433340,
   40 0x433150 (50, 51 and 5 have none). */
static const struct {
    int32_t cls;
    ClassNew make;
} classes[] = {
    {0, NULL},  {2, structure_new}, {4, structure_new}, {1, NULL},  {9, NULL},  {8, NULL},  {7, NULL},
    {10, class10_new}, {21, NULL}, {22, NULL}, {30, NULL}, {23, NULL}, {20, NULL}, {35, NULL},
    {36, NULL}, {34, NULL}, {52, NULL}, {31, NULL}, {32, NULL}, {33, NULL}, {53, NULL}, {50, NULL},
    {51, NULL}, {5, NULL},  {24, NULL}, {41, NULL}, {40, NULL},
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
    if (data && classes[class_index(cls)].make) free(data);
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

const Chunk *Chunk_List(const Node *n, int *count)
{
    const ChunkRec *r = chunk_find(n);
    *count = r ? r->count : 0;
    return r ? r->chunk : NULL;
}
