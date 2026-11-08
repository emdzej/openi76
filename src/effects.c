/* The effects (effects.h). */
#include "effects.h"
#include "chunks.h"
#include "crt.h"
#include "entity.h"
#include "exe.h"
#include "lod.h"
#include "objects.h"
#include "tex.h"
#include "clock.h"
#include "damage.h"
#include "texanim.h"
#include "render/fpu.h"
#include "render/light.h"
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

static void clear_flags_all(Node *n, uint32_t f)   /* 0x42e290 */
{
    if (n->next) clear_flags_all(n->next, f);
    if (n->child) clear_flags_all(n->child, f);
    n->flags &= ~f;
}

static Muzzle *muzzle_of(const Node *n)
{
    for (Muzzle *m = g_muzzles; m; m = m->next)
        if (m->node == n) return m;
    return NULL;
}

/* 0x454a60: the flash's animation done: its light out, the flash hidden again (flag 1 down its tree),
   the animation removed, the record idle */
static void flash_end(Node *flash)
{
    Muzzle *m = muzzle_of(flash->prev);   /* the flash's +0x68: the weapon's node */
    if (!m || !m->tex[0]) return;
    Light_Remove(flash, &flash->flags);
    set_flags_all(flash, 1);   /* 0x42e250 */
    TexAnim_Remove(flash, m->tex);
    m->f10 = 0;
}

void Muzzle_Flash(Node *muzzle)
{
    Node *flash = muzzle->child;
    if (!flash) return;
    clear_flags_all(flash, 1);
    Muzzle *m = muzzle_of(muzzle);
    if (!m) return;
    if (!Node_Live(muzzle)) {   /* tagged "mzlfls": the weapon freed, its record goes */
        /* the original unlinks the record only when it is the list's head (its search for the one before
           stops at the head itself), and frees it either way; the port unlinks it wherever it is */
        for (Muzzle **p = &g_muzzles; *p; p = &(*p)->next)
            if (*p == m) {
                *p = m->next;
                break;
            }
        /* the original then drops the record's reference again (0x41f6f0), on the node the check above has
           already released: the port's check took the reference, and the second is left out */
        free(m);
        return;
    }
    if (m->f10 == 1 && m->tex[0]) TexAnim_Remove(flash, m->tex);   /* a flash still going starts again */
    if (!m->tex[0]) return;
    if (m->f10 != 1) Light_Add(flash, &flash->flags, flash->frame.m, 0, 25.0f, 0, NULL, NULL);   /* 0x43a1f0 */
    TexAnim_Once(flash, m->tex, 0.1f, 0, m->w0, flash_end);   /* 0x47eb50 */
    m->f10 = 1;
}

void Muzzles_Init(void)
{
    while (g_muzzles) {
        Muzzle *m = g_muzzles;
        g_muzzles = m->next;
        free(m);
    }
}

Effect *Effect_Find(const uint8_t name[8])
{
    uint32_t n0, n1, k[2];
    memcpy(&n0, name, 4);
    memcpy(&n1, name + 4, 4);
    key_of(k, n0, n1);
    for (Effect *e = g_effects[bucket(k)]; e; e = e->next)
        if (e->key[1] == k[1] && e->key[0] == k[0]) return e;
    return NULL;
}

static Node *g_explosions;   /* 0x4db3e8: the explosions' root, an entity of its own */

/* 0x454510: a done explosion (its textures' last frame) off the root, its light out, and onto its
   effect's list for reuse */
static void explosion_end(Node *n)
{
    if (!n) return;
    Light_Remove(n, &n->flags);   /* 0x43a400 */
    Node_Detach(n);
    uint8_t *d = n->data;
    if (!d) return;
    Effect *e = Effect_Find(d);
    if (!e) return;
    Node **on = (Node **)Data_Ptr(d, 0x10);
    if (*on) {
        Node_Unref(*on);
        *on = NULL;
    }
    if (e->free) e->free->prev = n;
    n->prev = NULL;
    n->next = e->free;
    e->free = n;
}

Node *Explosion_Spawn(const uint8_t name[8], Node *owner, float x, float y, float z, Node *on)
{
    uint32_t n0, n1;
    memcpy(&n0, name, 4);
    memcpy(&n1, name + 4, 4);
    if (!n0 && !n1) return NULL;
    Effect *e = Effect_Find(name);
    if (!e) return NULL;
    Node *n = e->free;
    if (!n) {
        n = Node_Create(NULL);   /* 0x41f320, tagged "explsn" (0x4db410) */
        memcpy(n->name, e->key, 8);
        n->flags = 0x40;
        n->geom = NULL;
        Lod_Copy(e->tree, n);
        Class_Set(n, 0x34);
        memcpy(n->data, e->key, 8);
        for (int lod = 0; lod < 2; lod++) {
            const char *t = Node_FirstTexture(n, lod);
            if (t && *t) Tex_Bind((uintptr_t)n, t, t);
        }
    } else {
        e->free = n->next;
        n->next = NULL;
        n->prev = NULL;
        if (e->free) e->free->prev = NULL;
    }
    uint8_t *d = n->data;
    memcpy(&n->frame, exe_data(0x4db298, 0x40), 0x40);   /* the identity */
    n->frame.pos[0] = x;
    n->frame.pos[1] = y;
    n->frame.pos[2] = z;
    float life, until;
    memcpy(&life, &e->w[0], 4);
    until = fpu_f32(fpu_add(g_clock_time, life));
    memcpy(d + 8, &until, 4);
    *Data_Ptr(d, 0xc) = owner;
    *Data_Ptr(d, 0x10) = on;
    Node_Ref(on);   /* 0x41f440, tagged "explsn" */
    if (!g_explosions) {
        g_explosions = Node_Create(NULL);
        g_explosions->data = NULL;
        g_explosions->flags = 0x100;
        Entity_Add(g_explosions, NULL, NULL);
    }
    Node_Attach(n, g_explosions);
    Light_Add(n, &n->flags, n->frame.m, 0, 20.0f, 0, NULL, NULL);   /* 0x43a1f0 */
    for (int lod = 0; lod < 2; lod++) {
        const char *t = Node_FirstTexture(n, lod);
        if (t && *t) TexAnim_Once(n, t, 0.1f, 0, e->w[1], explosion_end);
    }
    float dmg, radius;
    memcpy(&dmg, &e->w[2], 4);
    memcpy(&radius, &e->w[3], 4);
    if ((uint32_t)e->w[2] & 0x7fffffff) Splash(x, z, radius, (int32_t)(int64_t)dmg, n, NULL);   /* 0x46a3e0 */
    Node **o = (Node **)Data_Ptr(d, 0x10);
    if (*o) *o = Node_Live(*o);   /* tagged "explsn" */
    return n;
}
