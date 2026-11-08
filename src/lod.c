/* The levels of detail (lod.h). */
#include "lod.h"
#include "geom.h"
#include "platform.h"
#include "tex.h"
#include <stdlib.h>
#include <string.h>

/* a state's names (0x74 bytes): seven levels of 16 bytes, then the next state */
typedef struct LodNames {
    char name[7][16];
    struct LodNames *next;   /* +0x70 */
} LodNames;

/* a node's record (0x40 bytes) */
struct LodRec {
    Node *node;              /* +0 */
    Geom *cur, *prev;        /* +4, +8 */
    int32_t state;           /* +0xc: the state shown (set by 0x47d?? on damage; 0 here) */
    int32_t lod, plod;       /* +0x10, +0x14: -1 at first */
    char cname[16];          /* +0x18 */
    char pname[16];          /* +0x28 */
    LodNames *names;         /* +0x38 */
    struct LodRec *next;     /* +0x3c */
};

#define BUCKETS 0x7ed
static LodRec *table[BUCKETS];   /* 0x57f558 */

static unsigned bucket(const Node *n) { return (unsigned)(((uintptr_t)n * 0x6cd + 0xaab) % BUCKETS); }

void Lod_Init(void) { memset(table, 0, sizeof table); }

const LodRec *Lod_Find(const Node *n)
{
    for (LodRec *r = table[bucket(n)]; r; r = r->next)
        if (r->node == n) return r;
    return NULL;
}

bool Lod_Add(Node *n, const char *name, int state, int lod)
{
    LodNames *l;
    if (lod == 0 && state == 0) {
        LodRec *r = calloc(1, sizeof *r);
        if (!r) plat_log("Failed to add object to the cache");   /* the original's fatal */
        r->node = n;
        r->lod = r->plod = -1;
        r->next = table[bucket(n)];
        table[bucket(n)] = r;
        l = r->names = calloc(1, sizeof *l);
    } else {
        /* the original reads through a missing record (its callers always made one first) */
        LodRec *r = (LodRec *)Lod_Find(n);
        l = r->names;
        if (state > 0) {
            for (int i = state - 1; i > 0; i--) l = l->next;
            if (!l->next) l->next = calloc(1, sizeof *l);
            l = l->next;
        }
    }
    if (name) strncpy(l->name[lod], name, 15);
    return true;
}

bool Lod_Select(Node *n, int lod)
{
    LodRec *r = (LodRec *)Lod_Find(n);
    if (!r) return false;
    int was = r->lod;
    if (was == lod) return n->geom != NULL;
    if (r->plod == lod) {   /* back to the previous: swap */
        r->lod = r->plod;
        r->plod = was;
        Geom *g = r->cur;
        r->cur = r->prev;
        r->prev = g;
        char t[16];
        memcpy(t, r->cname, 16);
        memcpy(r->cname, r->pname, 16);
        memcpy(r->pname, t, 16);
        n->geom = r->cur;
        return r->cur != NULL;
    }
    LodNames *l = r->names;
    for (int i = r->state; i > 0; i--) l = l->next;
    r->lod = lod;
    if (strcmp(r->cname, l->name[lod]) != 0) {
        if (r->prev) Geom_Release(r->prev);
        r->plod = was;
        r->prev = r->cur;
        memcpy(r->pname, r->cname, 16);
        strncpy(r->cname, l->name[lod], 15);
        r->cur = Geom_Get(l->name[lod]);
        n->geom = r->cur;
        Tex_Refresh((uintptr_t)n);
    }
    return n->geom != NULL;
}

bool Lod_SetState(Node *n, int state)
{
    LodRec *r = (LodRec *)Lod_Find(n);
    if (!r) return false;
    if (r->state == state) return true;
    LodNames *l = r->names;
    for (int i = state; i > 0; i--) l = l->next;
    r->state = state;
    if (r->lod == -1) return true;
    if (r->prev) {
        Geom_Release(r->prev);
        r->prev = NULL;
        memset(r->pname, 0, sizeof r->pname);
    }
    r->plod = -1;
    if (strcmp(r->cname, l->name[r->lod]) == 0) return true;
    if (r->cur) {
        Geom_Release(r->cur);
        r->cur = NULL;
    }
    strncpy(r->cname, l->name[r->lod], 15);
    r->cur = Geom_Get(l->name[r->lod]);
    n->geom = r->cur;
    Tex_Refresh((uintptr_t)n);
    return n->geom != NULL;
}

bool Lod_Copy(const Node *src, Node *dst)
{
    const LodRec *f = Lod_Find(src);
    if (!f) return false;
    LodRec *r = calloc(1, sizeof *r);
    if (!r) plat_log("Failed to add object to the cache");   /* the original's fatal */
    r->node = dst;
    r->lod = r->plod = -1;
    r->next = table[bucket(dst)];
    table[bucket(dst)] = r;
    LodNames **to = &r->names;
    for (const LodNames *l = f->names; l; l = l->next) {
        *to = calloc(1, sizeof **to);
        memcpy((*to)->name, l->name, sizeof l->name);
        to = &(*to)->next;
    }
    memcpy(dst->centre, src->centre, sizeof dst->centre);
    dst->radius = src->radius;
    memcpy(dst->bmin, src->bmin, sizeof dst->bmin);
    memcpy(dst->bmax, src->bmax, sizeof dst->bmax);
    r->plod = f->plod;   /* overwritten below, as the original's */
    r->lod = f->lod;
    r->state = f->state;
    r->plod = -1;
    strncpy(r->cname, f->cname, 15);
    r->cur = Geom_Get(r->cname);
    dst->geom = r->cur;
    return true;
}

void Lod_Unload(Node *n)
{
    LodRec *r = (LodRec *)Lod_Find(n);
    if (!r) return;
    if (r->prev) {
        Geom_Release(r->prev);
        r->prev = NULL;
    }
    if (r->cur) {
        Geom_Release(r->cur);
        r->cur = NULL;
    }
    r->lod = r->plod = -1;
    memset(r->cname, 0, sizeof r->cname);
    memset(r->pname, 0, sizeof r->pname);
    n->geom = NULL;
    Tex_Refresh((uintptr_t)n);
}

void Lod_UnloadTree(Node *n)
{
    Lod_Unload(n);
    for (Node *c = n->child; c; c = c->next) Lod_UnloadTree(c);
}

void Lod_Remove(Node *n)
{
    if (!n) return;
    LodRec **p = &table[bucket(n)];
    while (*p && (*p)->node != n) p = &(*p)->next;
    LodRec *r = *p;
    if (!r) return;
    *p = r->next;
    Geom_Release(r->cur);
    Geom_Release(r->prev);
    n->geom = NULL;
    for (LodNames *l = r->names, *nx; l; l = nx) {
        nx = l->next;
        free(l);
    }
    free(r);
}

void Lod_Info(const LodRec *r, const char **cur, const char **prev, int *lod, int *plod, int *state)
{
    *cur = r->cname;
    *prev = r->pname;
    *lod = r->lod;
    *plod = r->plod;
    *state = r->state;
}

const char *Lod_Name(const LodRec *r, int state, int lod)
{
    const LodNames *l = r->names;
    for (int i = state; i > 0 && l; i--) l = l->next;
    return l ? l->name[lod] : NULL;
}
