/* The scene graph (scene.h). In the current rounding mode (render/fpu.h). */
#include "scene.h"
#include "weapons.h"
#include "entity.h"
#include "exe.h"
#include "lod.h"
#include "platform.h"
#include "render/fpu.h"
#include "render/light.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define ADD fpu_add
#define MUL fpu_mul
#define F32 fpu_f32

Node *g_root;
Node *g_world_nodes;

static void identity(Frame *f) { memcpy(f, exe_data(0x4db298, sizeof *f), sizeof *f); }

Node *Node_Create(Node *parent)
{
    Node *n = calloc(1, sizeof *n);   /* HeapAlloc(0x4d3a38, HEAP_ZERO_MEMORY) */
    if (!n) return NULL;
    static uint32_t made;
    n->serial = ++made;
    n->facemask = 0xffffffff;
    n->f58 = -1;
    identity(&n->frame);
    if (parent) {
        if (parent->child) {
            parent->child->prev = n;
            n->next = parent->child;
        }
        n->prev = parent;
        parent->child = n;
        n->state = parent->state;
        n->refs = parent->refs;
        return n;
    }
    g_root = n;
    n->refs = 1;
    return n;
}

Node *Node_New(Node *parent, const char *name)
{
    Node *n = Node_Create(parent);
    if (n && name) memcpy(n->name, name, 8);
    return n;
}

/* the original's flag 0x4d3a40: the first call counts the node alone, the recursion whole sibling lists */
static void ref_list(Node *n)
{
    for (; n; n = n->next) {
        n->refs++;
        ref_list(n->child);
    }
}

void Node_Ref(Node *n)
{
    if (!n) return;
    n->refs++;
    ref_list(n->child);
}

Node *Node_Parent(Node *n)
{
    if (n->prev) {
        for (;;) {
            Node *p = n->prev;
            if (p->child == n) break;
            n = p;
            if (!p->prev) break;
        }
    }
    return n->prev;
}

/* the end of a node whose references ran out (the tail of 0x41f4b0 and 0x41f5e0): out of its siblings'
   chain, its children cut off, its class, meshes and light gone */
static void destroy(Node *n)
{
    Node *q = n->prev;
    if (q) {
        if (q->child == n) q->child = n->next;
        else q->next = n->next;
    }
    if (n->child) n->child->prev = NULL;
    if (n->next) n->next->prev = n->prev;
    Class_Free(n->cls, n->data);
    n->cls = 0;
    n->data = NULL;
    Lod_Remove(n);
    /* 0x47c030 (the textures' owner records) is not ported yet */
    if (n->flags & 0x800) Light_Remove(n, &n->flags);
    /* the original leaves the root 0x50a394 pointing at a freed node; the port clears it */
    if (g_root == n) g_root = NULL;
    if (Weapons_UseNode(n)) return;   /* kept: a weapon instance reads it on, as the original's freed memory */
    free(n);
}

/* 0x41f5e0: n's children and (below the first level, always) its siblings, then n, each losing drop
   references and marked freed */
static void release(Node *n, int depth, uint16_t drop)
{
    if (n->child) release(n->child, depth + 1, drop);
    if (n->next && depth > 1) release(n->next, depth + 1, drop);
    n->state = 1;
    n->refs -= drop;
    if (n->refs == 0) destroy(n);
}

void Node_Free(Node *n)
{
    if (!n) return;
    uint16_t drop = 1;
    if (n->prev) {
        Node *p = Node_Parent(n);
        if (p) drop = (uint16_t)p->refs;
        Node *q = n->prev;
        if (q->child == n) q->child = n->next;
        else if (q->next == n) q->next = n->next;
    }
    if (n->next) n->next->prev = n->prev;
    n->prev = NULL;
    n->next = NULL;
    if (n->child) release(n->child, 2, drop);
    n->state = 1;
    n->refs -= drop;
    if (n->refs == 0) destroy(n);
}

/* 0x41f860: one reference less on n, its siblings and their descendants */
static void unref_list(Node *n)
{
    for (; n; n = n->next) {
        n->refs--;
        if (n->child) unref_list(n->child);
    }
}

void Node_Unref(Node *n)
{
    if (!n) return;
    if (n->refs < 2) {
        Node_Free(n);
        return;
    }
    n->refs--;
    unref_list(n->child);
}

Node *Node_Live(Node *n)
{
    if (!n) return NULL;
    if (n->state == 1) {
        Node_Unref(n);
        return NULL;
    }
    return n;
}

/* 0x41fa90: delta references on n and all its descendants */
static void refs_add(Node *n, int32_t delta)
{
    if (!n) return;
    n->refs += delta;
    for (Node *c = n->child; c; c = c->next) refs_add(c, delta);
}

void Node_Detach(Node *n)
{
    Node *p = Node_Parent(n);
    int32_t d = p ? p->refs - 1 : 0;
    Node *q = n->prev;
    if (q) {
        if (q->child == n) q->child = n->next;
        else q->next = n->next;
    }
    if (n->next) n->next->prev = n->prev;
    n->refs -= d;
    n->prev = NULL;
    n->next = NULL;
    for (Node *c = n->child; c; c = c->next) {
        c->refs -= d;
        for (Node *cc = c->child; cc; cc = cc->next) refs_add(cc, -d);
    }
}

void Node_Attach(Node *n, Node *p)
{
    if (!p) return;
    n->prev = p;
    n->next = p->child;
    if (p->child) p->child->prev = n;
    p->child = n;
    n->state = p->state;
    int32_t d = p->refs - 1;
    n->refs += d;
    for (Node *c = n->child; c; c = c->next) {
        c->refs += d;
        for (Node *cc = c->child; cc; cc = cc->next) refs_add(cc, d);
    }
}

void Node_FrameIn(Frame *out, const Node *n, const Node *upto)
{
    if (n == upto) {
        identity(out);
        return;
    }
    Frame f = n->frame;
    while (n->prev && n->prev != upto) {
        const Node *p = n->prev;
        if (p->child == n) {
            Frame t;
            Frame_Mul(&t, &f, &p->frame);
            f = t;
        }
        n = p;
    }
    *out = f;
}

void Node_WorldPos(float out[3], const Node *n)
{
    Frame f;
    if (n) Node_FrameIn(&f, n, NULL);
    else identity(&f);
    for (int k = 0; k < 3; k++) out[k] = fpu_f32(f.pos[k]);
}

void Node_SetYaw(Node *n, float a)
{
    /* fsincos (0x452a70), its results stored as floats */
    float s = fpu_f32(sin(a)), c = fpu_f32(cos(a));
    float m[9] = {c, 0, -s, 0, 1, 0, s, 0, c};
    memcpy(n->frame.m, m, sizeof m);
}

Node *Node_Find(Node *n, const char *name)
{
    for (; n; n = n->next) {
        if (memcmp(n->name, name, 8) == 0) return n;
        Node *c = Node_Find(n->child, name);
        if (c) return c;
    }
    return NULL;
}

/* 0x42ec00: n and its siblings, each in f */
static void bounds(Node *n, const Frame *f, float min[3], float max[3])
{
    do {
        Frame m;
        Frame_Mul(&m, &n->frame, f);
        if (!(n->flags & 1) && Lod_Select(n, 0) && n->geom->nverts > 0) {
            for (int i = 0; i < n->geom->nverts; i++) {
                float p[3];
                Frame_Points(&p, &n->geom->verts[i], 1, &m);
                for (int k = 0; k < 3; k++)
                    if (min[k] > p[k]) min[k] = p[k];
                for (int k = 0; k < 3; k++)
                    if (max[k] < p[k]) max[k] = p[k];
            }
        }
        if (n->child) bounds(n->child, &m, min, max);
        n = n->next;
    } while (n);
}

void Node_Bounds(Node *n, float min[3], float max[3])
{
    Frame inv;
    Frame_Invert(&inv, &n->frame);
    min[0] = min[1] = min[2] = 1e7f;   /* 0x4b189680 */
    max[0] = max[1] = max[2] = -1e7f;
    bounds(n, &inv, min, max);
}

/* 0x42ea90: the largest squared distance, n and its siblings in f; the comparison is before the sum's
   rounding to a float */
static float radius2(Node *n, const Frame *f, float r2)
{
    do {
        Frame m;
        Frame_Mul(&m, &n->frame, f);
        if (!(n->flags & 1) && Lod_Select(n, 0) && n->geom->nverts > 0) {
            for (int i = 0; i < n->geom->nverts; i++) {
                float p[3];
                Frame_Points(&p, &n->geom->verts[i], 1, &m);
                double s = ADD(MUL(p[0], p[0]), ADD(MUL(p[1], p[1]), MUL(p[2], p[2])));
                if (s > r2) r2 = F32(s);
            }
        }
        if (n->child) r2 = radius2(n->child, &m, r2);
        n = n->next;
    } while (n);
    return r2;
}

float Node_Radius(Node *n, float cx, float cy, float cz)
{
    Frame c, m, inv;
    identity(&c);
    c.pos[0] = cx;
    c.pos[1] = cy;
    c.pos[2] = cz;
    Frame_Mul(&m, &c, &n->frame);
    Frame_Invert(&inv, &m);
    return F32(fpu_sqrt(radius2(n, &inv, 0)));
}

void World_ListInit(void) { g_world_nodes = NULL; }

void World_Add(Node *n)
{
    if (n->cls != 0x51 && n->cls != 5) {
        float min[3], max[3];
        Node_Bounds(n, min, max);
        float h = exe_f32(0x4a74c0);   /* 0.5 */
        for (int k = 0; k < 3; k++) n->centre[k] = F32(MUL(ADD(max[k], min[k]), h));
        n->radius = Node_Radius(n, n->centre[0], n->centre[1], n->centre[2]);
    }
    if (!g_world_nodes) {
        g_world_nodes = n;
        return;
    }
    n->next = g_world_nodes->next;
    g_world_nodes->next = n;
}
