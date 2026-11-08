/* The collision world's set-up (collide.h). In the current rounding mode (render/fpu.h). */
#include "collide.h"
#include "classes.h"
#include "clock.h"
#include "combat.h"
#include "platform.h"
#include "exe.h"
#include "lod.h"
#include "terrain.h"
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
    /* 0x577174, 0x57716c and the heaps (0x478e30, 0x4771b0) */
    Entity_OnAdd(Collide_Add);
    Entity_OnRemove(Collide_Remove);   /* 0x469cd0 */
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

void Collide_Frame(void)
{
    for (Cell *c = g_coll_moving; c; c = c->next) {
        Entity *e = c->value;
        float dt = g_clock_dt, p[3], v[3], sp;
        Frame_Points(&p, &e->c, 1, &e->node->frame);   /* 0x4240e0 */
        Class_Velocity(e->node, v, &sp);
        float *box = (float *)&e->rest[(0x50 - 0x34) / 4];
        box[0] = p[0];
        box[2] = p[2];
        box[3] = F32(ADD(MUL(v[0], dt), p[0]));
        box[5] = F32(ADD(MUL(v[2], dt), p[2]));
        if (box[3] < box[0]) {
            float t = box[0];
            box[0] = box[3], box[3] = t;
        }
        if (box[5] < box[2]) {
            float t = box[2];
            box[2] = box[5], box[5] = t;
        }
        box[0] = F32(SUB(box[0], e->r));
        box[2] = F32(SUB(box[2], e->r));
        box[3] = F32(ADD(box[3], e->r));
        box[5] = F32(ADD(box[5], e->r));
    }
    /* each moving entity against the moving after it and against every static one (0x469f80; with a
       static one touched, 0x48d530 merges their scene nodes): not ported yet */
    static int said;
    if (!said && g_coll_moving) {
        said = 1;
        plat_log("OpenI76: the collisions between entities (0x469f80) are not ported yet");
    }
}

/* 0x46b560: the meshes of type 0x3000 under n freed */
static void meshes_free(Node *n)
{
    for (; n; n = n->next) {
        if ((n->flags & 0xf000) == 0x3000) mesh_free(n);
        if (n->child) meshes_free(n->child);
    }
}

/* 0x46b5a0: each shown node with a mesh (level 0) under n gets its box (+0x94), centre (+0x84) and
   radius (+0x90) anew from its vertices */
static void nodes_measure(Node *n)
{
    for (; n; n = n->next) {
        if (!(n->flags & 1) && Lod_Select(n, 0)) {
            float mn[3] = {1e30f, 1e30f, 1e30f}, mx[3] = {-1e30f, -1e30f, -1e30f}, c[3], best = -1e30f;
            const Geom *g = n->geom;
            for (int i = 0; i < g->nverts; i++)
                for (int k = 0; k < 3; k++) {
                    float v = g->verts[i][k];
                    if (mn[k] > v) mn[k] = v;           /* test ah,0x41: an unordered one is not taken */
                    if (!(mx[k] >= v)) mx[k] = v;       /* test ah,1: it is */
                }
            memcpy(n->bmin, mn, 12);
            memcpy(n->bmax, mx, 12);
            for (int k = 0; k < 3; k++) c[k] = F32(ADD(MUL(mx[k], 0.5f), MUL(mn[k], 0.5f)));   /* 0x4a6510 */
            memcpy(n->centre, c, 12);
            for (int i = 0; i < g->nverts; i++) {
                float dx = F32(SUB(g->verts[i][0], c[0])), dy = F32(SUB(g->verts[i][1], c[1]));
                double dz = SUB(g->verts[i][2], c[2]);   /* fst: unrounded, times its float */
                double s2 = ADD(ADD(MUL(dz, F32(dz)), MUL(dy, dy)), MUL(dx, dx));
                if (s2 > best) best = F32(s2);
            }
            n->radius = F32(fpu_sqrt(best));
        }
        if (n->child) nodes_measure(n->child);
    }
}

void Collide_Refit(Node *n)
{
    Entity *e = Entity_OfNode(n);   /* 0x424060 */
    if (!e) return;
    if (e->has_a == 3) meshes_free(e->node);   /* 0x46b540 */
    for (GroundCell *g = e->a, *nx; g; g = nx) {   /* 0x4775c0 */
        nx = g->next;
        free(g);
    }
    e->a = NULL;
    nodes_measure(n);   /* from the node given, with its siblings: not the entity's root */
    register_entity(e);   /* 0x46b480 */
    ground_faces(e->node, e, e->node->cls == 0xc);   /* 0x4774e0: whatever the class (quirk) */
    float mn[3], mx[3];
    Node_Bounds(e->node, mn, mx);
    for (int k = 0; k < 3; k++) e->c[k] = F32(MUL(ADD(mx[k], mn[k]), 0.5f));
    e->r = Node_Radius(e->node, e->c[0], e->c[1], e->c[2]);
}

/* ---- the ground under a point ---- */

/* 0x479340: (px, pz) inside the face's outline in x and z (its corners in v): first a box test, then the
   edges crossing pz, to either side of px, counted until two decide (on an edge counts as inside) */
static int face_has(const CollFace *f, const float (*v)[3], float px, float pz)
{
    unsigned m = 0;
    for (int i = f->n; i > 0 && m != 0xf;) {
        const float *p = v[f->refs[--i]];
        m |= p[0] > px ? 2 : 1;
        m |= !(pz >= p[2]) ? 8 : 4;
    }
    if (m != 0xf || f->n == 0) return 0;
    const float *b = v[f->refs[0]];
    int left = 0, right = 0;
    for (int i = f->n - 1; i >= 0; i--) {
        const float *a = v[f->refs[i]];
        if ((pz <= a[2] || pz <= b[2]) && (a[2] <= pz || b[2] <= pz)) {
            if (a[2] == pz && b[2] == pz) {   /* along the edge */
                if (a[0] < px && b[0] < px) return 0;
                if (px < a[0] && px < b[0]) return 0;
                return 1;
            }
            if (a[2] != pz) {
                if (px <= a[0] || px <= b[0]) {
                    if (a[0] <= px || b[0] <= px) {
                        double t = ADD(SUB(MUL(SUB(pz, a[2]), fpu_div(SUB(b[0], a[0]), SUB(b[2], a[2]))), px), a[0]);
                        if (t < 0.0 || t != t) {   /* 0x4a6860 */
                            if (left) return 0;
                            if (right) return 1;
                            left = 1;
                            b = a;
                            continue;
                        }
                        float tf = F32(t);
                        int32_t bits;
                        memcpy(&bits, &tf, 4);
                        if (bits < 1) return 1;   /* on the edge */
                    }
                    if (right) return 0;
                    if (left) return 1;
                    right = 1;
                } else {
                    if (left) return 0;
                    if (right) return 1;
                    left = 1;
                }
            }
        }
        b = a;
    }
    return 0;
}

/* 0x479140: the face's plane's height at (px, pz) */
static double face_height(const CollFace *f, float px, float pz)
{
    return -fpu_div(ADD(ADD(MUL(f->plane[2], pz), MUL(f->plane[0], px)), f->d), f->plane[1]);
}

/* 0x477620: the first of the entity's walkable faces (its +0x18 chain: face, corners, next) over (px, pz) */
static int ground_face(float px, float pz, const Entity *e, float *h, float n[3])
{
    for (void *const *c = e->a; c; c = c[2]) {
        const CollFace *f = c[0];
        if (face_has(f, (const float (*)[3])c[1], px, pz)) {
            *h = F32(face_height(f, px, pz));
            memcpy(n, f->plane, 3 * sizeof *n);
            return 1;
        }
    }
    return 0;
}

static uint32_t ubits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

void Ground_Height(float px, float py, float pz, float *out, float normal[3])
{
    const float none = -1e30f;
    float band = none, below = none, nb[3] = {0}, nl[3] = {0};
    for (Cell *c = g_coll_ground; c; c = c->next) {
        const Entity *e = c->value;
        /* 0x424250: the entity's sphere, its centre through the node's frame */
        float ctr[1][3];
        Frame_Points(ctr, (const float (*)[3])e->c, 1, &e->node->frame);
        float dx = F32(SUB(px, ctr[0][0]));
        double dzu = SUB(pz, ctr[0][2]);
        float dz = F32(dzu);
        if (MUL(e->r, e->r) < ADD(MUL(dzu, dz), MUL(dx, dx))) continue;   /* the stored dz times its unrounded value */
        float h, n[3];
        if (!ground_face(px, pz, e, &h, n)) continue;
        if (ubits(band) == ubits(none) && py > h && below < h) {
            below = h;
            memcpy(nl, n, sizeof nl);
        }
        if (ADD(h, 3.0f) > py && SUB(h, 3.0f) < py && band < h) {   /* 0x4a6854: within 3 of py */
            band = h;
            memcpy(nb, n, sizeof nb);
        }
    }
    if (ubits(band) < 0xf149f2ca) {
        *out = band;
        if (normal) memcpy(normal, nb, sizeof nb);
    } else if (ubits(below) < 0xf149f2ca) {
        *out = below;
        if (normal) memcpy(normal, nl, sizeof nl);
    } else {
        *out = F32(Terrain_Height(px, pz));
        if (normal) Terrain_Normal(normal, px, pz);
    }
}

/* 0x469cd0, an entity's removal: off its collision list (the moving or the static by its class, 0x42e640)
   with its meshes (0x46b540), then off the ground list (0x477240: classes 11-13) with its walkable faces
   (0x4775c0). The original moves the lists' shared cursor on when it held the cell; the port's walks read
   the next cell first. */
void Collide_Remove(Entity *e)
{
    if (e->has_a) {
        int cls = e->node->cls;
        Cell **list = cls == 1 || cls == 7 || cls == 9 ? &g_coll_moving : &g_coll_static;
        for (Cell *c = *list; c; c = c->next)
            if (c->value == e) {
                if (e->has_a == 3) meshes_free(e->node);
                List_Remove(list, c);
                break;
            }
    }
    int cls = e->node->cls;
    if (cls > 10 && cls < 0xe)
        for (Cell *c = g_coll_ground; c; c = c->next)
            if (c->value == e) {
                for (GroundCell *g = e->a, *nx; g; g = nx) {
                    nx = g->next;
                    free(g);
                }
                e->a = NULL;
                List_Remove(&g_coll_ground, c);
                break;
            }
}
