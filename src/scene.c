/* The scene graph (scene.h). In the current rounding mode (render/fpu.h). */
#include "scene.h"
#include "entity.h"
#include "exe.h"
#include "lod.h"
#include "platform.h"
#include "render/fpu.h"
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

void Node_Free(Node *n)
{
    if (!n) return;
    int drop = 1;
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
    /* 0x41f5e0, the children's release, is not ported yet: every caller so far has none */
    if (n->child) plat_log("Node_Free: children not released (0x41f5e0 not ported)");
    n->state = 1;
    n->refs -= drop;
    if (n->refs == 0) {
        if (n->child) n->child->prev = NULL;
        Class_Free(n->cls, n->data);
        n->cls = 0;
        n->data = NULL;
        Lod_Remove(n);
        /* 0x47c030 (the textures' owner records) and 0x43a400 (a light) are not ported yet */
        /* the original leaves the root 0x50a394 pointing at a freed node; the port clears it */
        if (g_root == n) g_root = NULL;
        free(n);
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
