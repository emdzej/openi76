/* The objects' shadows (shadow.h). In the current rounding mode (render/fpu.h). */
#include "render/shadow.h"
#include "entity.h"
#include "exe.h"
#include "lod.h"
#include "terrain.h"
#include "render/fpu.h"
#include "render/light.h"
#include "render/queue.h"
#include <math.h>
#include <string.h>

#define ADD fpu_add
#define SUB fpu_sub
#define MUL fpu_mul
#define F32 fpu_f32

#define MAXV 8192
static float sbuf[MAXV][3];   /* 0x58dcb0, the drawers' vertex buffer */
static const View *s_view;    /* 0x58c43c */
static int32_t s_lod;         /* 0x58c438 */
static float s_dir[3];        /* 0x58c480: towards the light */

static int below(double a, double b) { return a < b || a != a || b != b; }
static int below_eq(double a, double b) { return a <= b || a != a || b != b; }
static uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

/* 0x4a35c0: where the segment from a (above the ground) to b (below it) meets the ground, halving it up
   to 10 times until the midpoint is within 0.2 of the height there; 0 if a is not above or b not below
   the ground, or no midpoint came within 0.2 */
static int ground_hit(float out[3], float a[3], float b[3])
{
    float ha = F32(Terrain_Height(a[0], a[2])), hb = F32(Terrain_Height(b[0], b[2]));
    if (!below(ha, a[1]) || below_eq(hb, b[1])) return 0;
    for (int i = 0; i < 10; i++) {
        out[0] = F32(MUL(ADD(b[0], a[0]), 0.5));   /* 0x4a7bc0 */
        out[1] = F32(MUL(ADD(b[1], a[1]), 0.5));
        double z = MUL(ADD(b[2], a[2]), 0.5);
        out[2] = F32(z);
        double d = SUB(Terrain_Height(out[0], z), out[1]);   /* z unrounded */
        if (below(fabs(d), 0.2f)) return 1;   /* 0x4a7bc8: 0.2f as a double */
        /* the midpoint below the ground (d positive, as bits) replaces b, above it a */
        memcpy(fbits(F32(d)) > 0x80000000u ? a : b, out, 3 * sizeof *out);
    }
    return 0;
}

/* 0x4a3870: the frame drawing n's mesh (m: its frame in the world) as its shadow on the ground under its
   centre, in camera space; 0 if there is no ground along the light within 128 or it is too steep */
static int shadow_frame(Frame *out, const Node *n, const Frame *m)
{
    float p[3], q[3], h[3];
    Frame_Points(&p, (const float (*)[3])n->centre, 1, m);
    for (int k = 0; k < 3; k++) q[k] = F32(ADD(MUL(s_dir[k], -128.0), p[k]));   /* 0x4a7bd4 */
    if (!ground_hit(h, p, q)) return 0;
    /* the ground's plane through three heights around the hit, 2 away (0x4a7bd8) */
    float a[3], b[3], c[3];
    a[0] = h[0];
    a[2] = F32(SUB(h[2], 2.0));
    b[0] = F32(ADD(h[0], 2.0));
    b[2] = F32(ADD(h[2], 2.0));
    c[0] = F32(SUB(h[0], 2.0));
    c[2] = b[2];
    a[1] = F32(Terrain_Height(a[0], a[2]));
    b[1] = F32(Terrain_Height(b[0], b[2]));
    c[1] = F32(Terrain_Height(c[0], c[2]));
    float plane[4];
    Plane_From(plane, a, c, b);
    if (!Frame_Project(out, plane, s_dir)) return 0;
    Frame t;
    Frame_Mul(&t, out, &s_view->cam);
    Frame_Mul(out, m, &t);
    return 1;
}

/* shadow_faces 0x4a36f0 + 0x4a3a80: n's faces turned to the light, projected; 0x4a36f0's (solid 0) every
   face mode 0xc0, 0x4a3a80's (a vehicle's, solid 1) 0xc0 for faces with their third flag, 0 for the others */
static void shadow_faces(Node *n, const Frame *m, int vehicle)
{
    Frame f;
    if (!shadow_frame(&f, n, m)) return;
    const float *r = m->m, *l = s_dir;
    /* the light in n's space; the third sum's order differs between the two */
    float l0 = F32(ADD(ADD(MUL(r[1], l[1]), MUL(r[2], l[2])), MUL(r[0], l[0])));
    float l1 = F32(ADD(ADD(MUL(r[5], l[2]), MUL(r[3], l[0])), MUL(r[4], l[1])));
    float l2 = vehicle ? F32(ADD(ADD(MUL(r[8], l[2]), MUL(r[6], l[0])), MUL(r[7], l[1])))
                       : F32(ADD(ADD(MUL(r[6], l[0]), MUL(r[7], l[1])), MUL(r[8], l[2])));
    Geom *g = n->geom;
    if (g->nverts > MAXV) return;
    Frame_Points(sbuf, (const float (*)[3])g->verts, g->nverts, &f);
    for (const GeomFace *gf = g->faces; gf; gf = gf->next) {
        if (below(ADD(ADD(MUL(gf->plane[2], l2), MUL(gf->plane[0], l0)), MUL(gf->plane[1], l1)), 0)) continue;
        /* the original's array on its stack holds about 15 corners; u and v are left as they were there */
        CamVert v[64];
        int k = gf->nrefs < 64 ? gf->nrefs : 64;
        memset(v, 0, sizeof v[0] * (size_t)k);
        for (int i = 0; i < k; i++) memcpy(&v[i].x, sbuf[gf->refs[i].vertex], 12);
        PolyArg arg;
        arg.colour = 0xfe;
        Queue_Poly(s_view, v, k, arg, vehicle && !gf->flags[2] ? 0 : 0xc0);
    }
}

/* 0x4a3da0 / 0x4a3e50: n and its siblings, each in parent */
static void shadow_tree(Node *n, const Frame *parent, int vehicle)
{
    do {
        Frame m;
        Frame_Mul(&m, &n->frame, parent);
        if (!(n->flags & 1) && Lod_Select(n, s_lod) && n->geom && n->geom->nverts >= 3) shadow_faces(n, &m, vehicle);
        if (n->child) shadow_tree(n->child, &m, vehicle);
        n = n->next;
    } while (n);
}

/* 0x4a3c20, through 0x48cd60 (types 3 and 0xb): the record's entity's children (not its own mesh), the
   vehicles' (class 9) by 0x4a3a80 */
static void shadow_draw(QRec *rec, const View *view)
{
    const Entity *e = rec->u.obj.node;
    s_view = view;
    s_lod = rec->u.obj.a;
    Light_ShadowDir(s_dir, e->node, e->c);
    Node *n = e->node;
    if (n->child) shadow_tree(n->child, &n->frame, n->cls == 9);
}

void Shadow_Init(void)
{
    Queue_SetDraw(3, shadow_draw);
    Queue_SetDraw(0xb, shadow_draw);
}
