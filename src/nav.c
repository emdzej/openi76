/* The AI's navigation (nav.h). In the current rounding mode (render/fpu.h). */
#include "nav.h"
#include "drive.h"
#include "entity.h"
#include "collide.h"
#include "exe.h"
#include "fsm.h"
#include "math3d.h"
#include "platform.h"
#include "roads.h"
#include "terrain.h"
#include "render/fpu.h"
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define ADD fpu_add
#define SUB fpu_sub
#define MUL fpu_mul
#define F32 fpu_f32

static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }
static void put32(uint8_t *p, int32_t v) { memcpy(p, &v, 4); }
static void putf(uint8_t *p, float v) { memcpy(p, &v, 4); }
static uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static int32_t ftol(double x) { return (int32_t)(int64_t)x; }   /* 0x496690: chopped */

/* ---- the pool (0x58dcf0) ----

   A node, 0x80 bytes: +0 x, +4 z (the cell's corner, metres), +8 its children (8, or 9 with a bridge),
   +0xc its key z * 100000 + x, +0x10 on a road (the terrain's class 2-4), +0x14 a bridge's link, +0x18 f,
   +0x20 g, +0x28 the step's cost, +0x30 h (doubles), +0x38 the parent, +0x3c the children, +0x60 the
   next free, +0x64 / +0x68 the two trees' balances (0 left-heavy, 1 right-heavy, 2 even), +0x6c / +0x70
   the cell tree's left and right, +0x74 / +0x78 the cost tree's. */

#define POOL 0x58dcf0u
#define POOL_N 1000
static _Alignas(8) uint8_t pool[POOL_N * 0x80];
static uint32_t free_list;   /* 0x505534 */
static int32_t used;         /* 0x4a92d8 */
static int32_t used_most;    /* 0x505528 */

static uint8_t *P(uint32_t a) { return pool + (a - POOL); }
static int32_t I(uint32_t a, int off) { return rd32(P(a) + off); }
static void SetI(uint32_t a, int off, int32_t v) { put32(P(a) + off, v); }
static double D(uint32_t a, int off) { double v; memcpy(&v, P(a) + off, 8); return v; }
static void SetD(uint32_t a, int off, double v) { memcpy(P(a) + off, &v, 8); }
static uint32_t U(uint32_t a, int off) { return (uint32_t)I(a, off); }

void Nav_Init(void)
{
    free_list = POOL;
    for (int i = 0; i < POOL_N - 1; i++) SetI(POOL + i * 0x80u, 0x60, (int32_t)(POOL + (i + 1) * 0x80u));
    SetI(POOL + (POOL_N - 1) * 0x80u, 0x60, 0);
    used = used_most = 0;   /* and 0x50553c */
}

/* 0x40fdf0 and the end of 0x412db0: the free list through the nodes the search reached, two more, and the
   last node's link cleared (0x5ad0d0). Past 999 reached the original links on into the skill tables after
   the pool (0x5ad0f0) whenever fewer than 1000 are in use; the port stops at the pool's end. */
static void pool_free(uint8_t *ai)
{
    used_most += 2;
    free_list = POOL;
    if (used > 999) used_most = 999;
    for (int i = 0; i < used_most && i < POOL_N - 1; i++) SetI(POOL + i * 0x80u, 0x60, (int32_t)(POOL + (i + 1) * 0x80u));
    if (used_most > POOL_N - 1) plat_log("OpenI76: the AI's search pool linked past its end (the original's bug)");
    used = used_most = 0;   /* and 0x50553c */
    SetI(POOL + (POOL_N - 1) * 0x80u, 0x60, 0);
    put32(ai + 0x7c, 0);
    put32(ai + 0x9cf0, 0);
    put32(ai + 0x9cf4, 0);
}

void Nav_Reset(uint8_t *ai) { pool_free(ai); }

/* 0x412670 */
static uint32_t pool_take(void)
{
    if (++used > used_most) used_most = used;
    if (!free_list) return 0;
    uint32_t a = free_list;
    free_list = U(a, 0x60);
    return a;
}

/* 0x4125c0: a node at (x, z) with g (and f) given; +0x14 is left as it was */
static uint32_t node_new(int32_t x, int32_t z, double g, uint32_t parent)
{
    uint32_t a = pool_take();
    if (!a) return 0;
    SetI(a, 0x38, (int32_t)parent);
    SetI(a, 0, x);
    SetI(a, 4, z);
    SetI(a, 0xc, z * 100000 + x);
    SetI(a, 0x10, 0);
    SetD(a, 0x20, g);
    SetD(a, 0x28, 0);
    SetD(a, 0x30, 0);
    SetD(a, 0x18, g);
    SetI(a, 8, 0);
    memset(P(a) + 0x3c, 0, 9 * 4);
    SetI(a, 0x64, 2);
    SetI(a, 0x68, 2);
    memset(P(a) + 0x6c, 0, 16);
    return a;
}

/* ---- the trees (0x41c9d0-0x41d010): AVL trees threaded through the nodes; tree 0 by the key, tree 1 by
   f then the key. A slot is the address of a 32-bit link (in the AI block or in a node). ---- */

typedef uint8_t *Slot;
static uint32_t sget(Slot s) { return (uint32_t)rd32(s); }
static void sset(Slot s, uint32_t a) { put32(s, (int32_t)a); }
static Slot link(uint32_t a, int t, int side) { return P(a) + 0x6c + (side + t * 2) * 4; }
static Slot bal(uint32_t a, int t) { return P(a) + 0x64 + t * 4; }

/* 0x412920 a before b, 0x412990 a after b */
static bool before(uint32_t a, uint32_t b, int t)
{
    if (t == 0) return I(a, 0xc) < I(b, 0xc);
    if (D(a, 0x18) < D(b, 0x18)) return true;
    return D(b, 0x18) == D(a, 0x18) && I(a, 0xc) < I(b, 0xc);
}
static bool after(uint32_t a, uint32_t b, int t)
{
    if (t == 0) return I(b, 0xc) < I(a, 0xc);
    if (D(b, 0x18) < D(a, 0x18)) return true;
    return D(b, 0x18) == D(a, 0x18) && I(b, 0xc) < I(a, 0xc);
}

/* 0x41cdb0: the rotation after an insert made side a too deep */
static void rotate_insert(Slot s, int t, int a, int b)
{
    uint32_t r = sget(s), c = sget(link(r, t, a));
    if (I(c, 0x64 + t * 4) == a) {
        sset(link(r, t, a), sget(link(c, t, b)));
        sset(link(c, t, b), r);
        put32(bal(r, t), 2);
        sset(s, c);
    } else {
        uint32_t g = sget(link(c, t, b));
        sset(link(c, t, b), sget(link(g, t, a)));
        sset(link(g, t, a), c);
        sset(link(r, t, a), sget(link(g, t, b)));
        sset(link(g, t, b), r);
        put32(bal(r, t), I(g, 0x64 + t * 4) == a ? b : 2);
        put32(bal(c, t), I(g, 0x64 + t * 4) == b ? a : 2);
        sset(s, g);
    }
    put32(bal(sget(s), t), 2);
}

/* 0x41c9d0: the rotation after a removal left side b too shallow (a the deep side); 1 when it shrank */
static int rotate_remove(Slot s, int t, int a, int b)
{
    uint32_t r = sget(s), c = sget(link(r, t, a));
    int cb = I(c, 0x64 + t * 4);
    if (cb != a && cb != 2) {
        uint32_t g = sget(link(c, t, b));
        sset(link(c, t, b), sget(link(g, t, a)));
        sset(link(g, t, a), c);
        sset(link(r, t, a), sget(link(g, t, b)));
        sset(link(g, t, b), r);
        put32(bal(r, t), I(g, 0x64 + t * 4) == a ? b : 2);
        put32(bal(c, t), I(g, 0x64 + t * 4) == b ? a : 2);
        sset(s, g);
        put32(bal(g, t), 2);
        return 1;
    }
    sset(link(r, t, a), sget(link(c, t, b)));
    sset(link(c, t, b), r);
    if (cb == 2) {
        put32(bal(r, t), a);
        put32(bal(c, t), b);
        sset(s, c);
        return 0;
    }
    put32(bal(r, t), 2);
    put32(bal(c, t), 2);
    sset(s, c);
    return 1;
}

/* 0x41cea0: n into the tree at s; 1 when it grew */
static int tree_insert(uint32_t n, Slot s, int t)
{
    uint32_t r = sget(s);
    if (!r) {
        sset(s, n);
        return 1;
    }
    if (!before(n, r, t)) {
        if (!tree_insert(n, link(r, t, 1), t)) return 0;
        r = sget(s);
        switch (I(r, 0x64 + t * 4)) {
        case 0: put32(bal(r, t), 2); return 0;
        case 2: put32(bal(r, t), 1); return 1;
        case 1: rotate_insert(s, t, 1, 0); return 0;
        }
        return 0;
    }
    if (!tree_insert(n, link(r, t, 0), t)) return 0;
    r = sget(s);
    switch (I(r, 0x64 + t * 4)) {
    case 1: put32(bal(r, t), 2); return 0;
    case 2: put32(bal(r, t), 0); return 1;
    case 0: rotate_insert(s, t, 0, 1); return 0;
    }
    return 0;
}

/* 0x41cfb0: the node with n's place in the tree */
static uint32_t tree_find(uint32_t n, uint32_t r, int t)
{
    while (r) {
        if (before(n, r, t)) r = sget(link(r, t, 0));
        else if (after(n, r, t)) r = sget(link(r, t, 1));
        else return r;
    }
    return 0;
}

/* 0x41d010: the first node taken out; 1 when the tree shrank */
static int tree_pop(uint32_t *out, Slot s, int t)
{
    uint32_t r = sget(s);
    if (!sget(link(r, t, 0))) {
        *out = r;
        sset(s, sget(link(r, t, 1)));
        return 1;
    }
    if (!tree_pop(out, link(r, t, 0), t)) return 0;
    r = sget(s);
    switch (I(r, 0x64 + t * 4)) {
    case 0: put32(bal(r, t), 2); return 1;
    case 2: put32(bal(r, t), 1); return 0;
    case 1: return rotate_remove(s, t, 1, 0);
    }
    return 0;
}

/* 0x41cb20: the last node of the subtree at sub takes the place of the one at s (given back in *out) */
static int tree_swap_last(Slot sub, Slot s, uint32_t *out, int t)
{
    uint32_t m = sget(sub);
    if (sget(link(m, t, 1))) {
        if (tree_swap_last(link(m, t, 1), s, out, t) != 1) return 0;
        m = sget(sub);
        switch (I(m, 0x64 + t * 4)) {
        case 1: put32(bal(m, t), 2); return 1;
        case 2: put32(bal(m, t), 0); return 0;
        case 0: return rotate_remove(sub, t, 0, 1);
        }
        return 0;
    }
    *out = sget(s);
    sset(s, m);
    sset(sub, sget(link(m, t, 0)));
    uint32_t r = sget(s), o = *out;
    put32(bal(r, t), I(o, 0x64 + t * 4));
    sset(link(r, t, 0), sget(link(o, t, 0)));
    sset(link(r, t, 1), sget(link(o, t, 1)));
    return 1;
}

/* 0x41cbf0: n's place taken out of the tree (the node given back in *out); 1 when it shrank */
static int tree_remove(uint32_t n, uint32_t *out, Slot s, int t)
{
    uint32_t r = sget(s);
    if (!r) return 0;
    if (before(n, r, t)) {
        if (!tree_remove(n, out, link(r, t, 0), t)) return 0;
        r = sget(s);
        switch (I(r, 0x64 + t * 4)) {
        case 0: put32(bal(r, t), 2); return 1;
        case 2: put32(bal(r, t), 1); return 0;
        case 1: return rotate_remove(s, t, 1, 0);
        }
        return 0;
    }
    if (after(n, r, t)) {
        if (!tree_remove(n, out, link(r, t, 1), t)) return 0;
        r = sget(s);
        switch (I(r, 0x64 + t * 4)) {
        case 1: put32(bal(r, t), 2); return 1;
        case 2: put32(bal(r, t), 0); return 0;
        case 0: return rotate_remove(s, t, 0, 1);
        }
        return 0;
    }
    if (!sget(link(r, t, 1))) {
        *out = r;
        sset(s, sget(link(r, t, 0)));
        return 1;
    }
    if (!sget(link(r, t, 0))) {
        *out = r;
        sset(s, sget(link(r, t, 1)));
        return 1;
    }
    if (tree_swap_last(link(r, t, 0), s, out, t) != 1) return 0;
    r = sget(s);
    switch (I(r, 0x64 + t * 4)) {
    case 0: put32(bal(r, t), 2); return 1;
    case 2: put32(bal(r, t), 1); return 0;
    case 1: return rotate_remove(s, t, 1, 0);
    }
    return 0;
}

/* ---- the bridges (0x50552c): a binary tree by x * 100000 + z of the cells a bridge links ---- */

typedef struct Bridge {
    double key;              /* +0: x * 100000 + z */
    int32_t x0, z0;          /* +8: the cell at this end */
    int32_t x, z;            /* +0x10: the cell at its other end */
    Node *obj;               /* +0x18: the bridge's object (flag 0x200: down) */
    struct Bridge *right;    /* +0x1c */
    struct Bridge *left;     /* +0x20 */
} Bridge;
static Bridge *g_bridges;    /* 0x50552c */
static Bridge g_links[500];  /* 0x505570, 0x28 each (the clear in 0x4109b0 runs to 0x50a3ac) */
static int g_nlinks;         /* 0x505568 */

static Bridge *bridge_at(double key)
{
    for (Bridge *b = g_bridges; b;) {
        if (fabs(b->key - key) < 1.0) return (b->obj->flags & 0x200) ? NULL : b;
        b = key <= b->key ? b->left : b->right;
    }
    return NULL;
}

/* Nav_LinkNew 0x40fe70: the next record, from the cell (x, z) to (x2, z2), each rounded to the nearest metre */
static Bridge *link_new(double x, double z, Node *obj, float x2, float z2)
{
    if (g_nlinks >= 500) {   /* the original runs on past its table */
        plat_log("OpenI76: the bridges' links table (0x505570) is full");
        return NULL;
    }
    Bridge *b = &g_links[g_nlinks++];
    b->x0 = ftol(floor(ADD(x, 0.5)));   /* 0x4a52f8 */
    b->z0 = ftol(floor(ADD(z, 0.5)));
    b->obj = obj;
    b->key = ADD(MUL((double)b->x0, 100000.0), (double)b->z0);   /* 0x4a5300 */
    b->x = ftol(floor((double)F32(ADD(x2, 0.5))));   /* 0x4a5308 */
    b->z = ftol(floor((double)F32(ADD(z2, 0.5))));
    b->right = b->left = NULL;
    return b;
}

/* Nav_LinkInsert 0x40ff80: into the tree, right of a smaller key, else left */
static void link_insert(Bridge *b, Bridge **at)
{
    while (*at) at = (*at)->key < b->key ? &(*at)->right : &(*at)->left;
    *at = b;
}

static void link_add(Bridge *b)
{
    if (!b) return;
    /* the original works out x0 % 5 (and z0 % 5) here and drops them */
    link_insert(b, &g_bridges);
}

/* Nav_FindDeck 0x40ffe0: the first class 0xd node among n, its siblings and their descendants, with
   its frame in the world (each node's frame inside f's) */
static int find_deck(Node *n, const Frame *f, Node **out, Frame *of)
{
    for (; n; n = n->next) {
        Frame w;
        Frame_Mul(&w, &n->frame, f);   /* 0x4527c0 */
        if (n->cls == 0xd) {
            *of = w;
            *out = n;
            return 1;
        }
        if (n->child && find_deck(n->child, &w, out, of)) return 1;
    }
    return 0;
}

/* Nav_BridgeLinks 0x4100a0: an object (class 0xc) with a deck (class 0xd, the first among its nodes)
   links the cells on its two banks. The deck's box, 5 m longer at each end, goes into the world; its
   length must lie along x or z (its z axis within 0.9 of one of them). The two ends are pushed out to
   the 10 m grid, and every 5 m across the deck (from one side of the box to within 2.5 m of the other)
   a link runs each way between the banks' cells. */
static void bridge_links(Entity *e)
{
    Frame id, w;
    memcpy(&id, exe_data(0x4db298, sizeof id), sizeof id);
    Node *deck = NULL;
    for (Node *n = e->node; n; n = n->next) {
        Frame f;
        Frame_Mul(&f, &n->frame, &id);   /* 0x4527c0 */
        if (n->cls == 0xd) {
            w = f;
            deck = n;
            break;
        }
        if (n->child && find_deck(n->child, &f, &deck, &w)) break;
    }
    if (!deck) return;
    float a[3] = {deck->bmin[0], deck->bmin[1], F32(SUB(deck->bmin[2], 5.0f))};   /* 0x4a5328 */
    float b[3] = {deck->bmax[0], deck->bmax[1], F32(ADD(deck->bmax[2], 5.0f))};
    Frame_Points(&a, (const float (*)[3])&a, 1, &w);   /* 0x452740 */
    Frame_Points(&b, (const float (*)[3])&b, 1, &w);
    /* the tests are on the words: signed above 0.9, or (unsigned) a negative beyond -0.9 */
    uint32_t m8, m6;
    memcpy(&m8, &w.m[8], 4);
    memcpy(&m6, &w.m[6], 4);
    int along;   /* 0: the links along z, the steps along x; 1: the other way */
    int32_t i;
    if ((int32_t)m8 > 0x3f666666) {
        i = ftol(b[2]), b[2] = F32(ADD((double)i, (double)(10 - i % 10)));
        i = ftol(a[2]), a[2] = F32(SUB((double)i, (double)(i % 10)));
        along = 0;
    } else if (m8 > 0xbf666666u) {
        i = ftol(b[2]), b[2] = F32(SUB((double)i, (double)(i % 10)));
        i = ftol(a[2]), a[2] = F32(ADD((double)i, (double)(10 - i % 10)));
        along = 0;
    } else if ((int32_t)m6 > 0x3f666666) {
        i = ftol(b[0]), b[0] = F32(ADD((double)i, (double)(10 - i % 10)));
        i = ftol(a[0]), a[0] = F32(SUB((double)i, (double)(i % 10)));
        along = 1;
    } else if (m6 > 0xbf666666u) {
        i = ftol(b[0]), b[0] = F32(SUB((double)i, (double)(i % 10)));
        i = ftol(a[0]), a[0] = F32(ADD((double)i, (double)(10 - i % 10)));
        along = 1;
    } else {
        plat_log("OpenI76: bridge not orthoginal");   /* the original's message, through the empty Msg_Nop 0x407470 */
        return;
    }
    int k = along ? 2 : 0;   /* the stepped axis */
    float step = b[k] > a[k] ? 5.0f : -5.0f;
    for (float c = a[k]; fabs(SUB(ADD(step, b[k]), c)) > 2.5; c = F32(ADD(c, step))) {   /* 0x4a5338 */
        if (!along) {
            link_add(link_new((double)c, (double)a[2], e->node, c, b[2]));
            link_add(link_new((double)c, (double)b[2], e->node, c, a[2]));
        } else {
            link_add(link_new((double)a[0], (double)c, e->node, b[0], c));
            link_add(link_new((double)b[0], (double)c, e->node, a[0], c));
        }
    }
}

/* Nav_Bridges 0x4109b0 */
void Nav_Bridges(void)
{
    g_nlinks = 0;
    g_bridges = NULL;
    memset(g_links, 0, sizeof g_links);   /* the original clears only each record's two links */
    for (int list = 0; list < 2; list++)   /* 0x46b1e0, 0x46b230: the moving entities, then the static */
        for (Cell *cl = list ? g_coll_static : g_coll_moving; cl; cl = cl->next) {
            Entity *e = (Entity *)cl->value;
            if (e->node->cls == 0xc) bridge_links(e);
        }
}

/* 0x4126b0: the eight cells round n (and the bridge's far end) as its children; 0 when the pool ran out */
static int expand(uint32_t n)
{
    int32_t x = ftol(floor((double)I(n, 0))), z = ftol(floor((double)I(n, 4)));
    double g = ADD(D(n, 0x28), D(n, 0x20));
    static const int8_t dx[8] = {-10, 0, 10, -10, 10, -10, 0, 10}, dz[8] = {-10, -10, -10, 0, 0, 10, 10, 10};
    for (int k = 0; k < 8; k++) {
        uint32_t c = node_new(x + dx[k], z + dz[k], g, n);
        SetI(n, 0x3c + k * 4, (int32_t)c);
        if (!c) return 0;
    }
    Bridge *b = bridge_at(ADD(MUL((double)x, 100000.0), (double)z));
    if (!b) {
        SetI(n, 0x14, 0);
        SetI(n, 8, 8);
        return 1;
    }
    SetI(n, 0x14, 1);
    uint32_t c = node_new(b->x, b->z, g, n);
    SetI(n, 0x5c, (int32_t)c);
    if (!c) return 0;
    SetI(n, 8, 9);
    return 1;
}

/* 0x412c50: n reached through p (its parent already or a better one); the costs passed down */
static void repath(uint32_t n, uint32_t p, Slot open)
{
    uint32_t old = U(n, 0x38);
    if (!old) return;
    bool better = false;
    if (old != p) {
        double g = ADD(D(p, 0x28), D(p, 0x20)), s = D(n, 0x28);
        int32_t x = I(n, 0), z = I(n, 4);
        if (I(old, 0) == x || I(old, 4) == z) {
            if (I(p, 0) != x && I(p, 4) != z) s = MUL(s, 1.4142135381698608);   /* 0x4a5388 */
        } else if (I(p, 0) == x || I(p, 4) == z)
            s = MUL(s, 0.7071067932881648);   /* 0x4a53a0 */
        double f = ADD(ADD(D(n, 0x30), s), g);
        if (f < D(n, 0x18)) {
            SetI(n, 0x38, (int32_t)p);
            SetD(n, 0x20, g);
            SetD(n, 0x28, s);
            SetD(n, 0x18, f);
            better = true;
        }
    }
    if (U(n, 0x38) != p) return;
    int32_t kids = I(n, 8);
    if (kids == -1) {
        uint32_t out = 0;
        tree_remove(n, &out, open, 1);
    }
    if (!better) {
        SetD(n, 0x20, ADD(D(p, 0x28), D(p, 0x20)));
        SetD(n, 0x18, ADD(ADD(D(n, 0x28), D(n, 0x30)), D(n, 0x20)));
    }
    if (kids == -1) {
        tree_insert(n, open, 1);
        return;
    }
    for (int k = 0; k < kids; k++) repath(U(n, 0x3c + k * 4), n, open);
}

/* ---- the path's passes ---- */

typedef struct { float x, z, flag; } Pt;   /* flag 0 open ground, 1 a road, 2 a bridge */
#define PATH_MAX 2000
#define F_ROAD 0x3f800000u
#define F_BRIDGE 0x40000000u

static void too_long(void) { plat_log("OpenI76: Astar ran out of memory - path to long"); }   /* 0x402c80 */

/* 0x41ae90: two road pieces of one RSEG */
static bool same_road(const RoadPiece *a, const RoadPiece *b) { return a && b && b->id == a->id; }

/* 0x41ba40: +1 when (b, ib) is ahead of (a, ia) along the strip, else -1 */
static int road_dir(const RoadPiece *a, int ia, const RoadPiece *b, int ib)
{
    if (b == a) return ib > ia ? 1 : -1;
    const RoadPiece *p = a;
    if (!p->last)
        for (;;) {
            if (p == b) return 1;
            p = p->next;
            if (p->last) break;
        }
    return p == b ? 1 : -1;
}

/* 0x4111a0: the points from (a, ia) to (b, ib) going dir */
static int road_count(int dir, const RoadPiece *a, int ia, const RoadPiece *b, int ib)
{
    if (dir == 1) {
        if (b == a) return ib - ia;
        int n = a->n - ia;
        while ((a = a->next) != b) n += a->n;
        return n + ib;
    }
    if (b == a) return ia - ib;
    if (!a->prev) return ia;
    int n = ia;
    while ((a = a->prev) != b) n += a->n;
    return n - ib + a->n;
}

/* 0x4110f0: the strip's first and last pieces from a; whether its ends meet (a loop, within 9 m) */
static bool road_loop(const RoadPiece *a, const RoadPiece **first, const RoadPiece **last)
{
    *first = a;
    while ((*first)->prev && (*first)->prev->last != 1) *first = (*first)->prev;
    *last = a;
    while (!(*last)->last) {
        if (!(*last)->next) {
            plat_log("OpenI76: road loop off the end of the road");   /* the original's fatal */
            break;
        }
        *last = (*last)->next;
    }
    const float *f = (*first)->pts[0], *l = (*last)->pts[(*last)->n - 1];
    double dz = SUB(f[5], l[5]), dx = SUB(f[3], l[3]);
    return ADD(MUL(dx, dx), MUL(dz, dz)) < 81.0;   /* 0x4a5358 */
}

/* 0x411220: on a loop, whether going dir round its join is shorter than n points */
static bool road_shorter(int dir, int n, const RoadPiece *a, int ia, const RoadPiece *last,
                         const RoadPiece *first, const RoadPiece *b, int ib)
{
    if (b == a) return false;
    int k;
    if (dir == 1) {
        k = a->n - ia;
        for (;;) {
            if (a->last) {
                for (;;) {
                    if (b == first) return k + ib < n;
                    k += first->n;
                    if (n < k) return false;
                    first = first->next;
                }
            }
            a = a->next;
            k += a->n;
            if (k > n) return false;
        }
    }
    k = ia;
    const RoadPiece *p = a->prev;
    while (p && !p->last) {
        k += p->n;
        if (n < k) return false;
        p = p->prev;
    }
    for (;;) {
        if (b == last) return last->n - ib + k < n;
        k += last->n;
        if (n < k) return false;
        last = last->prev;
    }
}

/* Road_Next 0x41ac40 (nav.h) */
int Road_Next(const RoadPiece **pp, int dir, int32_t *pi)
{
    const float *s = (*pp)->pts[*pi];
    float sx = s[3], sy = s[4], sz = s[5];
    for (int tries = 0;;) {
        if (dir == -1) {
            if (*pi == 0) {
                const RoadPiece *q = (*pp)->prev;
                if (!q || q->last) return 0;
                *pp = q;
                *pi = q->n - 1;
            } else
                --*pi;
        } else if ((*pp)->n - *pi != 1)
            ++*pi;
        else {
            if ((*pp)->last) return 0;
            *pp = (*pp)->next;
            *pi = 0;
        }
        const float *q = (*pp)->pts[*pi];
        float x = F32(SUB(q[3], sx)), y = F32(SUB(q[4], sy)), z = F32(SUB(q[5], sz));
        if (ADD(ADD(MUL(y, y), MUL(z, z)), MUL(x, x)) > 0.001) return 1;   /* 0x4a5668 */
        if (++tries > 10) {
            plat_log("OpenI76: roadwar - get_next_index in infinite loop");   /* the original's fatal */
            return 0;
        }
    }
}

/* 0x41ada0: the same point, or one of the same RSEG within 1 mm */
static bool road_same_point(const RoadPiece *a, int ia, const RoadPiece *b, int ib)
{
    if (b == a && ib == ia) return true;
    if (b->id != a->id) return false;
    const float *p = a->pts[ia], *q = b->pts[ib];
    float x = F32(SUB(p[3], q[3])), y = F32(SUB(p[4], q[4])), z = F32(SUB(p[5], q[5]));
    return ADD(ADD(MUL(y, y), MUL(z, z)), MUL(x, x)) < 0.001;
}

/* 0x410b20: the lane's point, 2.45 m in from the road's right edge (side 1) or its left (side 0) */
static void lane_point(const RoadPiece *r, int i, int side, float *ox, float *oz)
{
    const float *p = r->pts[i], *q;
    int e = side == 1 ? 3 : 0;
    bool fwd;
    if (side == 1) fwd = i < r->n - 1;
    else fwd = !(i > 0);
    q = r->pts[fwd ? i + 1 : i - 1];
    /* the forward neighbour on the right and the backward one on the left turn one way (+), the others
       the other */
    bool plus = side == 1 ? fwd : !fwd;
    float dx = F32(SUB(q[e], p[e]));
    double dzx = SUB(q[e + 2], p[e + 2]);
    float dz = F32(dzx);
    double l2x = ADD(MUL(dzx, dz), MUL(dx, dx));
    float l2 = F32(l2x);
    float cz = 1.0f, cx = 0.0f;
    if (l2x <= -9.999999747378752e-05 || (int32_t)fbits(l2) >= 0x38d1b717) {
        double inv = Fast_InvSqrt(l2);
        if (plus) {
            cz = F32(MUL(dx, inv));
            cx = F32(-MUL(inv, dz));
        } else {
            cz = F32(-MUL(dx, inv));
            cx = F32(MUL(inv, dz));
        }
    }
    *ox = F32(ADD(MUL(cx, 2.45f), p[e]));
    *oz = F32(ADD(MUL(cz, 2.45f), p[e + 2]));
}

static void put_lane(const RoadPiece *r, int i, int side, Pt *q, int *k)
{
    lane_point(r, i, side, &q[*k].x, &q[*k].z);
    q[*k].flag = 1.0f;
    ++*k;
}

/* 0x411310: the lane's points along the strip from (a, ia) to (b, ib), round a loop's join when shorter */
static void road_follow(const RoadPiece *a, int32_t ia, const RoadPiece *b, int32_t ib, Pt *q, int *k)
{
    int dir = road_dir(a, ia, b, ib);
    int n = road_count(dir, a, ia, b, ib);
    const RoadPiece *first, *last;
    bool round = false;
    if (road_loop(a, &first, &last) && road_shorter(-dir, n, a, ia, last, first, b, ib)) {
        round = true;
        dir = -dir;
    }
    int lastn = last->n, lasti = lastn - 1;
    const RoadPiece *p = a;
    int32_t i = ia;
    if (!round) {
        do {
            put_lane(p, i, dir == 1, q, k);
            if (road_same_point(p, i, b, ib)) return;
        } while (Road_Next(&p, dir, &i));
    } else if (dir == -1) {
        do {
            put_lane(p, i, 0, q, k);
            if (road_same_point(p, i, first, 0)) break;
        } while (Road_Next(&p, -1, &i));
        if (road_same_point(first, 0, last, lasti)) lasti = lastn - 2;
        p = last, i = lasti;
        for (;;) {
            put_lane(p, i, 0, q, k);
            if (road_same_point(p, i, b, ib)) break;
            if (!Road_Next(&p, -1, &i)) return;
        }
    } else {
        do {
            put_lane(p, i, 1, q, k);
            if (road_same_point(p, i, last, lasti)) break;
        } while (Road_Next(&p, dir, &i));
        i = road_same_point(last, lasti, first, 0) ? 1 : 0;
        p = first;
        for (;;) {
            put_lane(p, i, 1, q, k);
            if (road_same_point(p, i, b, ib)) break;
            if (!Road_Next(&p, dir, &i)) return;
        }
    }
}

static uint32_t pbits(float f) { return fbits(f); }

/* 0x4116a0: each step between two road points of one RSEG laid along the road's lane */
static void along_roads(const Pt *in, int n, Pt *out, int *nout)
{
    int k = 0;
    for (int i = 0; i < n - 1; i++) {
        const Pt *p = &in[i];
        if (pbits(p->flag) == F_ROAD && pbits(in[i + 1].flag) == F_ROAD) {
            RoadPiece *ra, *rb;
            int32_t ia = 0, ib = 0;
            Roads_Nearest(p->x, p->z, &ra, &ia);
            Roads_Nearest(in[i + 1].x, in[i + 1].z, &rb, &ib);
            if (same_road(ra, rb)) {
                i++;
                road_follow(ra, ia, rb, ib, out, &k);
                continue;
            }
            out[k++] = (Pt){p->x, p->z, 1.0f};
        } else if (pbits(p->flag) == F_ROAD)
            out[k++] = (Pt){p->x, p->z, 1.0f};
        else
            out[k++] = *p;
    }
    out[k++] = in[n - 1];
    if (k > 1999) too_long();
    *nout = k;
}

/* the step's direction from a to b (0x411850, 0x412030): 0 / 3 along z, 1 / 4 / 6 toward +x, 2 / 5 / 7
   toward -x */
static int step_dir(int32_t ax, int32_t az, int32_t bx, int32_t bz)
{
    if (ax < bx) return az < bz ? 1 : az > bz ? 4 : 6;
    if (ax > bx) return az < bz ? 2 : az > bz ? 5 : 7;
    return az < bz ? 0 : 3;
}

/* 0x411850: the cells from n back to the start, a point where the direction, the road or the bridge
   changes */
static void cells_back(uint32_t n, Pt *out, int *nout)
{
    int last_road = -1, last_bridge = -1, last_dir = -1;
    *nout = 0;
    while (U(n, 0x38)) {
        int32_t road = I(n, 0x10), bridge = I(n, 0x14);
        uint32_t p = U(n, 0x38);
        int dir = step_dir(I(n, 0), I(n, 4), I(p, 0), I(p, 4));
        if (dir != last_dir || road != last_road || I(p, 0x10) != road || bridge != last_bridge ||
            bridge != I(p, 0x14)) {
            out[*nout] = (Pt){(float)I(n, 0), (float)I(n, 4), bridge ? 2.0f : road ? 1.0f : 0.0f};
            if (++*nout > 1999) too_long();
        }
        n = p;
        last_road = road, last_bridge = bridge, last_dir = dir;
    }
    out[*nout] = (Pt){(float)I(n, 0), (float)I(n, 4), I(n, 0x14) ? 2.0f : I(n, 0x10) ? 1.0f : 0.0f};
    if (++*nout > 1999) too_long();
}

/* 0x410a00: whether direction d keeps to the run's general direction *run */
static int same_way(const int *run, int d)
{
    switch (*run) {
    case 0: return d >= 0 && d < 3;
    case 1: return d >= 0 && (d < 2 || d == 6);
    case 2: return d > 1 && (d < 4 || d == 7);
    case 3: return d > 2 && d < 6;
    case 4: return d > 2 && (d < 5 || d == 6);
    case 5: return d == 3 || d == 5 || d == 7;
    case 6: return d == 1 || d == 4 || d == 6;
    case 7: return d == 2 || d == 5 || d == 7;
    }
    plat_log("OpenI76: FSM - astar in illigal state");   /* the original's fatal */
    return 0;
}

/* 0x412030: runs of open ground going one general way cut to their ends, unless the terrain's flag (0x48e280)
   lies between them, sampled in steps */
static void cut_runs(const Pt *in, int n, Pt *out, int *nout)
{
    int i = 0, k = 1;
    out[0] = in[0];
    do {
        int j = i + 1;
        int run = step_dir(ftol(in[i].x), ftol(in[i].z), ftol(in[j].x), ftol(in[j].z));
        if (!(pbits(in[j].flag) & 0x7fffffff) && j + 1 < n) {
            bool go;
            do {
                int d = step_dir(ftol(in[j].x), ftol(in[j].z), ftol(in[j + 1].x), ftol(in[j + 1].z));
                go = !(pbits(in[j].flag) & 0x7fffffff) && same_way(&run, d);   /* this point's flag */
                if (go) j++;
                if (j + 1 >= n) go = false;
            } while (go);
        }
        bool again = i < j;
        while (again) {
            const Pt *t = &in[j];
            if (i - j != -1 && pbits(t->flag) != F_ROAD && pbits(t->flag) != F_BRIDGE) {
                float z0 = in[i].z, x0 = in[i].x;
                double dzx = SUB(t->z, z0), dxx = SUB(t->x, x0);
                float dz = F32(dzx), dx = F32(dxx);
                double l2x = ADD(MUL(dx, dx), MUL(dz, dz));
                float l2 = F32(l2x), inv = 1.0f;
                if (l2x <= -9.999999747378752e-05 || (int32_t)fbits(l2) >= 0x38d1b717) inv = F32(Fast_InvSqrt(l2));
                float end = F32(ADD(inv, 1.0f));   /* 0x4a534c */
                float sx = F32(MUL(MUL(inv, dx), 5.0f)), sz = F32(MUL(MUL(inv, dz), 5.0f));   /* 0x4a5328 */
                float t_ = 0;
                bool blocked = false;
                if ((int32_t)fbits(end) > 0) {
                    float x = x0, z = z0;
                    do {
                        x = F32(ADD(sx, x));
                        z = F32(ADD(sz, z));
                        if (Terrain_Flag(x, z)) {
                            blocked = true;
                            break;
                        }
                        double tt = ADD(inv, t_);
                        t_ = F32(tt);
                        if (!(tt < end)) break;
                    } while (1);
                }
                if (blocked) {
                    j--;
                    continue;
                }
            }
            again = false;
            out[k++] = in[j];
            i = j;
        }
    } while (i < n - 1);
    *nout = k;
}

/* 0x410e40: runs of road points kept to their ends where they lie on one RSEG (else to the farthest that
   does) */
static void join_roads(const Pt *in, int n, Pt *out, int *nout)
{
    static RoadPiece *road[PATH_MAX];
    static int32_t idx[PATH_MAX];
    memset(road, 0, sizeof road[0] * (size_t)(n > 0 ? n : 0));
    int k = 0, i = 0;
    while (n > i) {
        if (pbits(in[i].flag) == F_ROAD && n - 1 > i) {
            out[k++] = in[i];
            if (pbits(in[i + 1].flag) != F_ROAD) {
                i++;
                continue;
            }
            int j = i + 1;
            while (j < n - 1 && pbits(in[j + 1].flag) == F_ROAD) j++;
            if (!road[i]) Roads_Nearest(in[i].x, in[i].z, &road[i], &idx[i]);
            if (!road[j]) Roads_Nearest(in[j].x, in[j].z, &road[j], &idx[j]);
            if (same_road(road[i], road[j]) || i - j == -1) {
                out[k++] = in[j];
                i = j + 1;
                continue;
            }
            j--;
            while (j != i) {
                if (!road[j]) Roads_Nearest(in[j].x, in[j].z, &road[j], &idx[j]);
                if (same_road(road[i], road[j])) break;
                j--;
            }
            if (j != i) out[k++] = in[j];
            i = j + 1;
        } else {
            out[k++] = in[i];
            i++;
        }
    }
    *nout = k;
}

/* Nav_OntoBridges 0x411a20: a point on a bridge's cell and the next (its far end) moved onto the deck's
   lane: across the deck (its x axis) by 7.55, 2.45 or 1.55 m, the way and the amount by where the point
   lies from the deck's centre (Dir_Cos with its z axis, then with its x axis, 0.01 either side of square
   counting as on it); the two then joined by points every 5 m, flagged bridge, until within 5 m of the
   far end. Both points are moved in the input. */
static void onto_bridges(Pt *in, Pt *out, int *n)
{
    static Frame deck;   /* the stack's in the original: kept from the bridge before when one has no deck */
    int k = 0;
#define PUT(px, pz, pf) do { if (k < PATH_MAX + 7) out[k] = (Pt){px, pz, pf}; k++; } while (0)
    for (int i = 0; *n - 1 > 0 && i < *n - 1; i++) {
        Pt *p = &in[i], *q = &in[i + 1];
        if (pbits(p->flag) != F_BRIDGE) {
            PUT(p->x, p->z, p->flag);
            if (k > 1999) too_long();
            continue;
        }
        Bridge *r = bridge_at(ADD(MUL((double)p->x, 100000.0), (double)p->z));   /* 0x4a5300, within 0x4a5310 */
        if (!r)   /* the original reads through the null record here */
            plat_log("OpenI76: a bridge's point (0x411a20) with no standing bridge");
        Frame id;
        memcpy(&id, exe_data(0x4db298, sizeof id), sizeof id);
        for (Node *o = r ? r->obj : NULL; o; o = o->next) {
            Frame f;
            Frame_Mul(&f, &o->frame, &id);   /* 0x4527c0 */
            if (o->cls == 0xd) {
                deck = f;
                break;
            }
            if (o->child && find_deck(o->child, &f, &o, &deck)) break;
        }
        float cx = F32(deck.pos[0]), cz = F32(deck.pos[2]);
        float ux = deck.m[0], uz = deck.m[2];
        double along = Dir_Cos(cx, cz, F32(ADD(deck.m[6], cx)), F32(ADD(deck.m[8], cz)), p->x, p->z);
        double across = Dir_Cos(cx, cz, F32(ADD(ux, cx)), F32(ADD(uz, cz)), p->x, p->z);
        float s = F32(across);
        float by;   /* the shift along the x axis */
        if (along < 0.0) {   /* 0x4a5320 */
            if (across < -0.009999999776482582) by = 7.55f;   /* 0x4a5360, 0x4a5364 */
            else if ((int32_t)fbits(s) > 0x3c23d70a) by = -2.45f;   /* 0x4a5354 */
            else by = 1.55f;   /* 0x4a536c */
        } else {
            if (across > 0.009999999776482582) by = -7.55f;   /* 0x4a5368 */
            else if (fbits(s) > 0xbc23d70au) by = 2.45f;
            else by = -1.55f;
        }
        double tx = MUL(ux, fabsf(by)), tz = MUL(uz, fabsf(by));   /* unrounded */
        if (by > 0) {
            p->x = F32(ADD(p->x, tx)), p->z = F32(ADD(p->z, tz));
            q->x = F32(ADD(tx, q->x)), q->z = F32(ADD(tz, q->z));
        } else {
            p->x = F32(SUB(p->x, tx)), p->z = F32(SUB(p->z, tz));
            q->x = F32(SUB(q->x, tx)), q->z = F32(SUB(q->z, tz));
        }
        float dx = F32(SUB(q->x, p->x)), dz = F32(SUB(q->z, p->z));
        double l2x = ADD(MUL(SUB(q->z, p->z), dz), MUL(dx, dx));   /* z's unrounded difference by its stored one */
        float l2 = F32(l2x), step;
        if (!(l2x <= -9.999999747378752e-05) && (int32_t)fbits(l2) < 0x38d1b717)   /* 0x4a5344 */
            step = 1e6f;
        else
            step = F32(MUL(Fast_InvSqrt(l2), 5.0));   /* 0x4a5378 */
        dx = F32(MUL(step, dx)), dz = F32(MUL(step, dz));
        PUT(p->x, p->z, p->flag);
        float x = p->x, z = p->z;
        while (ADD(MUL(SUB(q->x, x), SUB(q->x, x)), MUL(SUB(q->z, z), SUB(q->z, z))) > 25.0) {   /* 0x4a5380 */
            x = F32(ADD(dx, x)), z = F32(ADD(dz, z));
            PUT(x, z, 2.0f);
        }
        PUT(q->x, q->z, q->flag);
        if (k >= 2000) too_long();
        i++;
    }
    PUT(in[*n - 1].x, in[*n - 1].z, in[*n - 1].flag);
#undef PUT
    if (k > PATH_MAX + 7) k = PATH_MAX + 7;   /* the port's: the original runs on past its buffer */
    *n = k;
}

/* 0x411fb0: the points into the AI block (+0xb0, 0x14 each: x, z, on a road, on a bridge); +0xa8 the next */
static void path_store(uint8_t *ai, const Pt *in, int n)
{
    for (int i = 0; i < n; i++) {
        uint8_t *o = ai + 0xb0 + i * 0x14;
        putf(o, in[i].x);
        putf(o + 4, in[i].z);
        uint32_t f = pbits(in[i].flag);
        put32(o + 8, f == F_ROAD);
        put32(o + 12, f == F_BRIDGE);
    }
    put32(ai + 0xac, n);
    put32(ai + 0xa8, 0);
}

/* 0x412420: the path to n */
static void path_out(uint32_t n, uint8_t *ai)
{
    uint32_t p = U(n, 0x38);
    if (!p) {
        putf(ai + 0xb0, (float)I(n, 0));
        putf(ai + 0xb4, (float)I(n, 4));
        put32(ai + 0xac, 1);
        put32(ai + 0xb8, I(n, 0x10));
        return;
    }
    if (!U(p, 0x38)) {
        putf(ai + 0xb0, (float)I(p, 0));
        putf(ai + 0xb4, (float)I(p, 4));
        put32(ai + 0xb8, I(p, 0x10));
        putf(ai + 0xc4, (float)I(n, 0));
        putf(ai + 0xc8, (float)I(n, 4));
        put32(ai + 0xac, 2);
        put32(ai + 0xcc, I(n, 0x10));
        return;
    }
    static Pt a[PATH_MAX + 8], b[PATH_MAX + 8];
    int na, nb;
    cells_back(n, a, &na);
    for (int i = 0; i < na; i++) b[i] = a[na - 1 - i];
    cut_runs(b, na, a, &na);
    join_roads(a, na, b, &nb);
    along_roads(b, nb, a, &na);
    onto_bridges(a, b, &na);
    path_store(ai, b, na);
}

/* 0x412a00: the start and the point both by one road's points (within 111) of one RSEG: the road's lane
   between them is the path */
static int on_one_road(float sx, float sz, float gx, float gz, uint8_t *ai)
{
    RoadPiece *ra, *rb;
    int32_t ia = 0, ib = 0;
    const float near = 111.0f;   /* 0x4a539c */
    Roads_Nearest(sx, sz, &ra, &ia);
    if (!ra) return 0;
    const float *p = ra->pts[ia];
    double rx = SUB(sx, p[3]), rz = SUB(sz, p[5]);
    float lx = F32(SUB(sx, p[0])), lz = F32(SUB(sz, p[2]));
    if (!(ADD(MUL(rx, rx), MUL(rz, rz)) <= near) && !(ADD(MUL(lz, lz), MUL(lx, lx)) <= near)) return 0;
    Roads_Nearest(gx, gz, &rb, &ib);
    if (!rb) return 0;
    p = rb->pts[ib];
    rx = SUB(gx, p[3]), rz = SUB(gz, p[5]);
    lx = F32(SUB(gx, p[0])), lz = F32(SUB(gz, p[2]));
    if (!(ADD(MUL(rx, rx), MUL(rz, rz)) <= near) && !(ADD(MUL(lz, lz), MUL(lx, lx)) <= near)) return 0;
    if (!same_road(ra, rb)) return 0;
    Pt two[2] = {{sx, sz, 1.0f}, {gx, gz, 1.0f}};
    static Pt out[PATH_MAX + 8];
    int n;
    along_roads(two, 2, out, &n);
    path_store(ai, out, n);
    return 1;
}

/* the cell's cost of entering c from cur (0x412db0): the terrain's class 2-4 a road (0.5), 6-7 0.5, the
   rest 15 (and 1e6 more on the terrain's flag when +0x9d14 asks); x 1.4142135 diagonally */
static void cost(uint8_t *ai, uint32_t cur, uint32_t c)
{
    SetD(c, 0x20, ADD(D(cur, 0x28), D(cur, 0x20)));
    double x = (double)I(c, 0), z = (double)I(c, 4);
    float base, extra = 0;
    switch (Terrain_Class(x, z)) {
    case 2: case 3: case 4: base = 0.5f; SetI(c, 0x10, 1); break;
    case 6: case 7: base = 0.5f; SetI(c, 0x10, 0); break;
    default:
        SetI(c, 0x10, 0);
        base = 15.0f;
        if (rd32(ai + 0x9d14) && Terrain_Flag(x, z)) extra = 1e6f;
    }
    double k = (I(cur, 0) == I(c, 0) || I(c, 4) == I(cur, 4)) ? 1.0 : 1.4142135381698608;
    SetD(c, 0x28, MUL(ADD(extra, base), k));
    SetD(c, 0x18, ADD(ADD(D(c, 0x28), D(c, 0x30)), D(c, 0x20)));
}

/* h: the octile distance to the goal, 1.414 a diagonal (0x4a5390) */
static void heuristic(uint32_t c, float gx, float gz)
{
    float ax = (float)fabs(SUB(gx, (double)I(c, 0)));
    double azx = fabs(SUB(gz, (double)I(c, 4)));   /* compared before its store */
    float az = (float)azx, lo, hi;
    if (azx < ax) hi = ax, lo = az;
    else hi = az, lo = ax;
    SetD(c, 0x30, ADD(SUB(hi, lo), MUL(lo, 1.414)));
    SetD(c, 0x18, ADD(ADD(D(c, 0x30), D(c, 0x28)), D(c, 0x20)));
}

static double dist2(uint32_t n, float gx, float gz)
{
    double a = SUB((double)I(n, 0), gx), b = SUB((double)I(n, 4), gz);
    return ADD(MUL(a, a), MUL(b, b));
}

/* the cell of a coordinate: ldiv by 10, the remainder over 5 rounding up (toward +inf only for x > 0) */
static int32_t cell_of(double v)
{
    int32_t t = ftol(v), q = t / 10, r = t % 10;
    return q * 10 + ((double)r > 5.0 ? 10 : 0);   /* 0x4a5378 */
}

int Nav_Search(uint8_t *ai, int32_t *count)
{
    uint32_t path = (uint32_t)rd32(ai + 0x88);
    uint32_t pts = (uint32_t)Fsm_Word(path + 0x54);
    int32_t at = rd32(ai + 0xa0);
    int32_t wz = Fsm_Word(pts + (uint32_t)at * 12 + 8), wx = Fsm_Word(pts + (uint32_t)at * 12);
    float gx, gz;
    memcpy(&gx, &wx, 4);
    memcpy(&gz, &wz, 4);
    Slot open = ai + 0x9cf0, all = ai + 0x9cf4;
    uint32_t cur = 0;
    int fail = 0;
    if (!sget(all)) {
        const Frame *f = *(const Frame **)Data_Ptr(ai, 0);
        int32_t sx = cell_of(f->pos[0]), sz = cell_of(f->pos[2]);
        if (on_one_road((float)sx, (float)sz, gx, gz, ai)) return 0;
        cur = node_new(sx, sz, 0, 0);
        int cls = Terrain_Class((double)sx, (double)sz);
        SetI(cur, 0x10, cls >= 2 && cls <= 4);
        expand(cur);
        for (int k = 0; k < I(cur, 8); k++) {
            uint32_t c = U(cur, 0x3c + k * 4);
            cost(ai, cur, c);
            heuristic(c, gx, gz);
            tree_insert(c, open, 1);
            tree_insert(c, all, 0);
        }
    }
    int steps = 0;
    if (!sget(open)) {   /* the original would fault popping an empty tree */
        pool_free(ai);
        return 0;
    }
    tree_pop(&cur, open, 1);
    if (sget(open)) {
        for (;;) {
            if (dist2(cur, gx, gz) < 121.0 || steps >= 100) break;   /* 0x4a5398 */
            if (!expand(cur)) {
                fail = 1;
                break;
            }
            for (int k = 0; k < I(cur, 8); k++) {
                uint32_t c = U(cur, 0x3c + k * 4);
                cost(ai, cur, c);
                uint32_t seen = tree_find(c, sget(all), 0);
                if (seen) {
                    if (ADD(D(c, 0x28), D(c, 0x20)) < ADD(D(seen, 0x28), D(seen, 0x20))) {
                        SetI(seen, 0x38, (int32_t)cur);
                        repath(seen, cur, open);
                    }
                    SetI(cur, 0x3c + k * 4, (int32_t)seen);
                    SetI(c, 0x60, (int32_t)free_list);
                    free_list = c;
                    used--;
                } else {
                    heuristic(c, gx, gz);
                    tree_insert(c, open, 1);
                    tree_insert(c, all, 0);
                }
            }
            steps++;
            tree_pop(&cur, open, 1);
            if (!sget(open)) break;
        }
    }
    put32(ai + 0x7c, rd32(ai + 0x7c) + steps);
    *count += steps;
    if (!(dist2(cur, gx, gz) < 121.0) && !fail) return 1;
    path_out(cur, ai);
    pool_free(ai);
    return 0;
}
