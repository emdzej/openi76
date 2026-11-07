/* The effects (effects.h). */
#include "effects.h"
#include "chunks.h"
#include "crt.h"
#include "entity.h"
#include "exe.h"
#include "lod.h"
#include "objects.h"
#include "tex.h"
#include <stdlib.h>
#include <string.h>

Effect *g_effects[EFFECT_BUCKETS];
Muzzle *g_muzzles;

static int32_t rd32(const uint8_t *p) { return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24); }

static void key_of(uint32_t k[2], uint32_t n0, uint32_t n1)
{
    uint8_t b[8];
    memcpy(b, &n0, 4);
    memcpy(b + 4, &n1, 4);
    for (int i = 0; i < 8; i++)
        if (b[i] >= 'a' && b[i] <= 'z') b[i] -= 32;   /* toupper 0x497870 */
    memcpy(&k[0], b, 4);
    memcpy(&k[1], b + 4, 4);
}

static unsigned bucket(const uint32_t k[2]) { return ((k[1] ^ k[0]) * 0x6cdu + 0xaabu) % EFFECT_BUCKETS; }

int Effects_Register(uint32_t n0, uint32_t n1, Node *tree, int32_t p4, int32_t p5, int32_t p6, int32_t p7)
{
    if (!tree) return 1;
    uint32_t k[2];
    key_of(k, n0, n1);
    unsigned b = bucket(k);
    for (Effect *e = g_effects[b]; e; e = e->next)
        if (e->key[1] == k[1] && e->key[0] == k[0]) return 0;
    Effect *e = calloc(1, sizeof *e);
    memcpy(e->key, k, sizeof k);
    e->next = g_effects[b];
    g_effects[b] = e;
    e->tree = tree;
    e->w[0] = p4;
    e->w[1] = p5;
    e->w[2] = p6;
    e->w[3] = p7;
    return 1;
}

/* 0x42e250 */
static void set_flags_all(Node *n, uint32_t f)
{
    if (n->next) set_flags_all(n->next, f);
    if (n->child) set_flags_all(n->child, f);
    n->flags |= f;
}

const char *Node_FirstTexture(Node *n, int lod)
{
    for (; !n->geom && lod < 6; lod++) Lod_Select(n, lod);
    if (!n->geom) return NULL;
    for (const GeomFace *f = n->geom->faces; f; f = f->next) {
        const char *dot;
        if (f->texture[0] && (dot = strchr(f->texture, '.')) && crt_stricmp(dot, exe_str(0x4de0a0)) != 0)
            return f->texture;
    }
    return NULL;
}

int Muzzles_Add(uint32_t n0, uint32_t n1, Node *node, int32_t p4, int32_t p5)
{
    if (!node->child) return 1;
    Muzzle *m = calloc(1, sizeof *m);
    m->next = g_muzzles;
    g_muzzles = m;
    m->name[0] = n0;
    m->name[1] = n1;
    m->node = node;
    Node_Ref(node);
    m->w6 = p4;
    m->w0 = p5;
    set_flags_all(node->child, 1);   /* the flash is not drawn until fired */
    const char *t = Node_FirstTexture(node->child, 0);
    if (t && *t) {
        strncpy(m->tex, t, 16);
        Tex_Bind((uintptr_t)node->child, m->tex, m->tex);
    }
    return 1;
}

/* the context of an .xdf (16 words): the node, the name's out, the name, the tree, XDFC's words */
typedef struct {
    Node *node;
    uint8_t *out;
    uint32_t name[2];
    Node *tree;
    int32_t w[10];
    int32_t pad;
} XdfCtx;

/* XDFC 0x4933e0 */
static int xdfc(const uint8_t *c, void *arg, const uint8_t *end)
{
    (void)end;
    XdfCtx *x = arg;
    for (int i = 0; i < 10; i++) x->w[i] = rd32(c + 8 + 4 * i);
    return 1;
}

/* XGEO 0x493400: three levels of the count at +8 */
static int xgeo(const uint8_t *c, void *arg, const uint8_t *end)
{
    (void)end;
    XdfCtx *x = arg;
    int32_t count = rd32(c + 8);
    const uint8_t *lists[3];
    Obj_Lists(lists, c + 0xc, 3, count);
    const uint8_t *first = lists[0];
    if (x->out) {
        if (crt_strnicmp((const char *)first, exe_str(0x4a90f4), 4) == 0) {
            memset(x->out, 0, 8);
            memcpy(x->out, "null", 4);
        } else {
            memcpy(x->out, first, 8);
        }
    }
    memcpy(x->name, first, 8);
    x->tree = Obj_Tree100(lists, x->node, 3, 1, count, NULL);
    return x->tree != NULL;
}

static ChunkDesc xdf_table[] = {   /* 0x4eb698 */
    {"XDFC", xdfc, 0x800}, {"XGEO", xgeo, 0x800}, {"EXIT", Objects_Exit, 0x1000},
};

static int xdf(const char *name, XdfCtx *x)
{
    if (!Bwd2_Read(name, xdf_table, 3, x)) return 1;
    if (x->node) {
        Muzzles_Add(x->name[0], x->name[1], x->node, x->w[6], x->w[0]);
        return 1;
    }
    if (x->tree && !Effects_Register(x->name[0], x->name[1], x->tree, x->w[6], x->w[0], x->w[7], x->w[8])) {
        Class_Unlist(x->tree);
        Node_Free(x->tree);
    }
    return 1;
}

int Xdf_Load(const char *name, uint8_t *out, Node *node)
{
    XdfCtx x;
    memset(&x, 0, sizeof x);
    x.node = node;
    x.out = out;
    memset(out, 0, 8);
    strncpy((char *)out, exe_str(0x4a90f4), 8);   /* "null" */
    return xdf(name, &x);
}

int Xdf_Load2(const char *name, uint8_t *out, Node *node)
{
    XdfCtx x;
    memset(&x, 0, sizeof x);
    x.node = node;
    x.out = out;
    return xdf(name, &x);
}

void Effects_Init(void)
{
    /* the original makes new heaps and leaves the last mission's records in the old ones; the port
       frees the records (not their trees, which the original keeps too) */
    for (int i = 0; i < EFFECT_BUCKETS; i++)
        while (g_effects[i]) {
            Effect *e = g_effects[i];
            g_effects[i] = e->next;
            free(e);
        }
    uint8_t out[8];
    Xdf_Load(exe_str(0x4db404), out, NULL);
    Xdf_Load(exe_str(0x4db3f8), out, NULL);
    Xdf_Load(exe_str(0x4db3ec), out, NULL);
}

void Muzzles_Init(void)
{
    while (g_muzzles) {
        Muzzle *m = g_muzzles;
        g_muzzles = m->next;
        free(m);
    }
}
