/* The collision world's set-up (collide.h). In the current rounding mode (render/fpu.h). */
#include "collide.h"
#include "exe.h"
#include "lod.h"
#include "render/fpu.h"
#include <stdlib.h>
#include <string.h>

#define ADD fpu_add
#define SUB fpu_sub
#define MUL fpu_mul
#define F32 fpu_f32

Cell *g_coll_static, *g_coll_moving, *g_coll_ground;
static uint32_t coll_type;   /* 0x577178: the type for nodes without one */

void Collide_Init(void)
{
    g_coll_moving = g_coll_static = g_coll_ground = NULL;
    /* 0x577174, 0x57716c, the removal callback 0x469cd0 and the heaps (0x478e30, 0x4771b0) */
    Entity_OnAdd(Collide_Add);
}

/* 0x479020 */
static void mesh_free(Node *n)
{
    CollMesh *m = n->coll;
    if (!m) return;
    for (CollFace *f = m->faces, *nx; f; f = nx) {
        nx = f->next;
        free(f);
    }
    free(m);
    n->coll = NULL;
}

/* 0x478e70: the node's mesh through f; the planes' normals rounded before their negation, d after */
static CollMesh *mesh_build(Node *n, const Frame *f)
{
    const Geom *g = n->geom;
    CollMesh *m = malloc(sizeof *m + (size_t)g->nverts * 12);
    if (!m) return NULL;
    m->nverts = g->nverts;
    m->verts = (float (*)[3])(m + 1);
    m->faces = NULL;
    Frame_Points(m->verts, (const float (*)[3])g->verts, g->nverts, f);
    const float *r = f->m;
    for (const GeomFace *gf = g->faces; gf; gf = gf->next) {
        int k = gf->nrefs;
        if (k < 3) continue;
        CollFace *c = malloc(sizeof *c + (size_t)k * 4);
        if (!c) continue;
        c->n = k;
        c->refs = (int32_t *)(c + 1);
        for (int i = 0; i < k; i++) c->refs[i] = (int32_t)gf->refs[i].vertex;
        const float *p = gf->plane;
        float t0 = F32(ADD(ADD(MUL(r[6], p[2]), MUL(r[3], p[1])), MUL(p[0], r[0])));
        float t1 = F32(ADD(ADD(MUL(r[1], p[0]), MUL(r[7], p[2])), MUL(r[4], p[1])));
        float t2 = F32(ADD(ADD(MUL(r[2], p[0]), MUL(r[8], p[2])), MUL(r[5], p[1])));
        c->plane[0] = -t0;
        c->plane[1] = -t1;
        c->plane[2] = -t2;
        const float *v = m->verts[c->refs[0]];
        c->d = F32(-ADD(ADD(MUL(v[1], c->plane[1]), MUL(v[2], c->plane[2])), MUL(v[0], c->plane[0])));
        c->next = m->faces;
        m->faces = c;
    }
    return m;
}

/* 0x46b830: n and its siblings, each in f */
static void set_type(Node *n, const Frame *f)
{
    for (; n; n = n->next) {
        Frame m;
        Frame_Mul(&m, &n->frame, f);
        uint32_t t = n->flags & 0xf000;
        if (t == 0x3000) mesh_free(n);
        if (t == 0) t = coll_type;
        if (t == 0x1000 || n->cls == 0x37 || (n->flags & 1) || !Lod_Select(n, 0) || !n->geom) {
            n->flags = (n->flags & 0xffff1fff) | 0x1000;
        } else {
            if (t == 0x3000) {
                /* a mesh that cannot be made leaves 0x1000 for the moment: the line after sets 0x3000 */
                if (n->geom->nverts < 3 || !(n->coll = mesh_build(n, &m))) n->flags = (n->flags & 0xffff1fff) | 0x1000;
                else n->flags = (n->flags & 0xffff3fff) | 0x3000;
            }
            n->flags = ((t ^ n->flags) & 0xf000) ^ n->flags;
        }
        if (n->child) set_type(n->child, &m);
    }
}

/* 0x46b480 */
/* 0x46b950: a vehicle's radius covers its COLP box (the entity's record: three counts, then the twelve
   words, whose corners are picked by the counts), plus 0.1 (0x4a6524) */
static void vehicle_radius(Entity *e)
{
    const int32_t *r = e->a;
    const float *f = (const float *)r;
    if (!r) return;
    int i6 = r[0] + 2, i10 = r[1] + 6, i14 = r[2] + 10;
    const int c[8][3] = {{3, 8, 11}, {i6, 8, 11}, {i6, 8, i14}, {3, 8, i14},
                         {3, i10, 11}, {i6, i10, 11}, {i6, i10, i14}, {3, i10, i14}};
    float best = -1e30f;
    for (int k = 0; k < 8; k++) {
        float dx = F32(SUB(f[c[k][0]], e->c[0])), dy = F32(SUB(f[c[k][1]], e->c[1]));
        double dzu = SUB(f[c[k][2]], e->c[2]);   /* fst: dz stays unrounded on the stack, times its float */
        double s = ADD(ADD(MUL(dzu, F32(dzu)), MUL(dy, dy)), MUL(dx, dx));
        if (s > best) best = F32(s);   /* fcom: unordered does not take it */
    }
    double nr = ADD(fpu_sqrt(best), 0.1f);
    if (nr > e->r) e->r = F32(nr);
}

static void register_entity(Entity *e)
{
    switch (e->node->cls) {
    case 1: case 9:   /* 0x46b950 */
        vehicle_radius(e);
        return;
    case 2: case 7: case 8: case 10:
        coll_type = 0x2000;
        break;
    case 3: case 0xb: case 0xc:
        coll_type = 0x3000;
        break;
    case 4:
        coll_type = 0x5000;
        break;
    default:
        return;
    }
    e->has_a = 3;
    Frame id;
    memcpy(&id, exe_data(0x4db298, sizeof id), sizeof id);
    set_type(e->node, &id);
}

/* a walkable face (12 bytes) */
typedef struct GroundCell {
    CollFace *face;
    float (*verts)[3];
    struct GroundCell *next;
} GroundCell;

/* 0x477510 / 0x477570: the walkable faces (normal y's bits above 0.4's) of the meshes under n, onto the
   entity's list; with roads only, those of class 13 */
static void ground_faces(Node *n, Entity *e, int roads)
{
    for (; n; n = n->next) {
        if (n->coll && (!roads || n->cls == 0xd))
            for (CollFace *f = n->coll->faces; f; f = f->next) {
                int32_t y;
                memcpy(&y, &f->plane[1], 4);
                GroundCell *g;
                if (y > 0x3ecccccd && (g = malloc(sizeof *g))) {
                    g->face = f;
                    g->verts = n->coll->verts;
                    g->next = e->a;
                    e->a = g;
                }
            }
        if (n->child) ground_faces(n->child, e, roads);
    }
}

void Collide_Add(Entity *e)
{
    register_entity(e);
    if (e->has_a) {
        int cls = e->node->cls;
        if (cls != 1 && cls != 7 && cls != 9) {   /* 0x42e640 */
            g_coll_static = List_Push(g_coll_static, NULL, e);
            float c[3];
            Frame_Points(&c, &e->c, 1, &e->node->frame);   /* 0x4240e0 */
            float r = e->r;
            float *box = (float *)&e->rest[(0x50 - 0x34) / 4];
            box[0] = F32(SUB(c[0], r));
            box[2] = F32(SUB(c[2], r));
            box[3] = F32(ADD(r, c[0]));
            box[5] = F32(ADD(r, c[2]));
        } else {
            g_coll_moving = List_Push(g_coll_moving, NULL, e);
        }
    }
    /* 0x477200 */
    int cls = e->node->cls;
    if (cls > 10 && cls < 0xe) {
        ground_faces(e->node, e, cls == 0xc);
        if (e->a) g_coll_ground = List_Push(g_coll_ground, NULL, e);
    }
}
