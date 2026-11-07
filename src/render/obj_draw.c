/* Drawing the objects (obj_draw.h). In the current rounding mode (render/fpu.h). */
#include "render/obj_draw.h"
#include "entity.h"
#include "exe.h"
#include "lod.h"
#include "platform.h"
#include "settings.h"
#include "tex.h"
#include "render/fpu.h"
#include "render/light.h"
#include "render/queue.h"
#include <string.h>

#define ADD fpu_add
#define SUB fpu_sub
#define MUL fpu_mul
#define DIV fpu_div
#define F32 fpu_f32

Frame g_heading;
/* camera.c's (its header's g_view would clash with this file's) */
extern int32_t g_cockpit;          /* 0x4a847c */
extern Entity *g_camera_entity;    /* 0x4a8660 */

static uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
/* the x87's flags of st0 : x (C0 below or unordered) */
static int below(double a, double b) { return a < b || a != a || b != b; }

void Render_SetHeading(const View *view)
{
    float a = -view->cam.m[2], b = -view->cam.m[8];
    double f = ADD(MUL(a, a), MUL(b, b));
    if (below(f, 9.99999975e-06f)) {   /* 0x4a50f4 */
        a = 0;
        b = -1;
    } else {
        double inv = Fast_InvSqrt(F32(f));
        float invf = F32(inv);
        b = F32(MUL(inv, b));
        a = F32(MUL(a, invf));
    }
    memset(g_heading.m, 0, sizeof g_heading.m);
    g_heading.m[0] = b;
    g_heading.m[2] = -a;
    g_heading.m[4] = 1;
    g_heading.m[6] = a;
    g_heading.m[8] = b;
}

/* ---- the mesh drawers ---- */

/* a face's record (from the pool at 0x58dcb4, 0x48 bytes and its vertices) */
typedef struct FaceRec {
    int32_t f0;                    /* +0 */
    int32_t n;                     /* +4: 0 for a splitting plane or a face turned away */
    Node *node;                    /* +8 */
    char tex[16];                  /* +0xc */
    uint32_t colour;               /* +0x1c */
    uint32_t mode;                 /* +0x20 */
    float c[3];                    /* +0x24: the centre, camera space */
    float plane[4];                /* +0x30: camera space */
    struct FaceRec *front, *back;  /* +0x40, +0x44 */
    CamVert v[];                   /* +0x48 */
} FaceRec;

#define ARENA (1 << 21)
#define MAXV 8192
static _Alignas(8) uint8_t arena[ARENA];   /* 0x58dcb4 (the terrain's node pool) */
static float cvbuf[MAXV * 4];               /* 0x58dcb0: the camera-space vertices, then their q */
static const View *g_view;                  /* 0x5509f0 */
static int32_t g_lod;                       /* 0x550a08 */
static uint32_t g_mode_or;                  /* 0x550a04: 0x100 for an object crossing the frustum */
static uint8_t *cur;                        /* 0x5509e4 */
static FaceRec *tree;                       /* 0x550a50 */

static void start(const View *view, const QObj *o)
{
    g_view = view;
    g_lod = o->a;
    cur = arena;
    tree = NULL;
}

static int room(int n) { return cur + sizeof(FaceRec) + (size_t)n * sizeof(CamVert) <= arena + ARENA; }

/* a centre into camera space: ((y m3 + z m6) + x m0) + pos, by column */
static void centre(float out[3], const float c[3], const Frame *f)
{
    const float *m = f->m;
    for (int j = 0; j < 3; j++)
        out[j] = F32(ADD(ADD(ADD(MUL(c[1], m[3 + j]), MUL(c[2], m[6 + j])), MUL(c[0], m[j])), f->pos[j]));
}

/* the tree insertions: 0x434a40 sums ((y + z) + x), the others ((x + z) + y); front when not below 0 */
static void insert(FaceRec *r, int xzy)
{
    if (!tree) {
        tree = r;
        return;
    }
    for (FaceRec *t = tree;;) {
        const float *p = t->plane;
        double s = xzy ? ADD(ADD(ADD(MUL(p[0], r->c[0]), MUL(p[2], r->c[2])), MUL(p[1], r->c[1])), p[3])
                       : ADD(ADD(ADD(MUL(p[1], r->c[1]), MUL(p[2], r->c[2])), MUL(p[0], r->c[0])), p[3]);
        FaceRec **next = below(s, 0) ? &t->back : &t->front;
        if (!*next) {
            *next = r;
            return;
        }
        t = *next;
    }
}

/* 0x4351f0: a splitting plane (a face of the container's own mesh) */
static FaceRec *plane_rec(const GeomFace *gf, const Frame *cam)
{
    FaceRec *r = (FaceRec *)cur;
    r->front = r->back = NULL;
    r->n = 0;
    centre(r->c, gf->centre, cam);
    Plane_Transform(r->plane, gf->plane, cam);
    r->n = 0;
    cur += sizeof(FaceRec);
    return r;
}

/* the container's mesh as splitting planes */
static void planes(Node *n, const View *view)
{
    if (!n->geom || !n->geom->faces) return;
    Frame cam;
    Frame_Mul(&cam, &n->frame, &view->cam);
    for (const GeomFace *gf = n->geom->faces; gf && room(0); gf = gf->next) insert(plane_rec(gf, &cam), 1);
}

/* 0x434a40: the node's faces (frame: the node in its entity's space, or the world) */
static void face_records(Node *n, const Frame *frame, const View *view)
{
    Geom *g = n->geom;
    GeomFace *gf = g->faces;
    if (!gf || g->nverts > MAXV) return;
    float (*cv)[3] = (float (*)[3])cvbuf;
    float *qs = cvbuf + 3 * g->nverts;
    const Frame *w = frame;
    if (n->flags & 0x40) {   /* the heading frame at the node's position */
        memcpy(g_heading.pos, frame->pos, sizeof g_heading.pos);
        w = &g_heading;
    }
    Frame cam;
    Frame_Mul(&cam, w, &view->cam);
    Frame_Points(cv, (const float (*)[3])g->verts, g->nverts, &cam);
    Light_Vertices(qs, (const float (*)[3])cv, (const float (*)[3])g->normals, frame->m, cam.m, g->nverts);
    for (; gf; gf = gf->next) {
        if (!room(gf->nrefs)) return;
        FaceRec *r = (FaceRec *)cur;
        r->front = r->back = NULL;
        r->n = 0;
        uint8_t *next = cur + sizeof(FaceRec);
        int build = 1;
        if (gf->nrefs < 3) {
            memset(r->c, 0, sizeof r->c);
            memset(r->plane, 0, sizeof r->plane);
        } else {
            centre(r->c, gf->centre, &cam);
            Plane_Transform(r->plane, gf->plane, &cam);
            build = (int32_t)fbits(r->plane[3]) < 1;   /* facing the camera (d not positive) */
        }
        if (build) {
            r->node = n;
            strncpy(r->tex, gf->texture, 15);
            r->colour = gf->colour;
            if (!gf->flags[0]) gf->flags[0] = 4;   /* in the mesh itself */
            uint32_t mode = exe_u32(0x4d6078u + 4u * ((uint32_t)gf->flags[1] + (uint32_t)gf->flags[0] * 8));
            if (gf->flags[2]) mode |= 0x80;
            r->mode = (mode & n->facemask) | g_mode_or;
            r->n = gf->nrefs;
            for (int k = 0; k < gf->nrefs; k++) {
                const GeomRef *ref = &gf->refs[k];
                CamVert *v = &r->v[k];
                memcpy(&v->x, cv[ref->vertex], 12);
                v->u = ref->u;
                v->v = ref->v;
                v->q = qs[ref->normal];
            }
            next = cur + sizeof(FaceRec) + (size_t)gf->nrefs * sizeof(CamVert);
        }
        insert(r, 0);
        cur = next;
    }
}

/* 0x434e70: n and its siblings, each in frame */
static void child_records(Node *n, const Frame *frame, const View *view)
{
    do {
        Frame m;
        Frame_Mul(&m, &n->frame, frame);
        if (!(n->flags & 1) && Lod_Select(n, g_lod) && n->geom && n->geom->nverts) face_records(n, &m, view);
        if (n->child) child_records(n->child, &m, view);
        n = n->next;
    } while (n);
}

/* the children of a container n, each in n's frame */
static void children(Node *n, const View *view)
{
    for (Node *c = n->child; c; c = c->next) {
        Frame m;
        Frame_Mul(&m, &c->frame, &n->frame);
        if (!(c->flags & 1) && Lod_Select(c, g_lod) && c->geom && c->geom->nverts) face_records(c, &m, view);
        if (c->child) child_records(c->child, &m, view);
    }
}

/* a face as the drawers draw it: its texture, if it has one and the name finds it (the colour is
   replaced by the lookup's result even when that is none) */
static void draw_face(const View *view, Node *owner, const char *tex, uint32_t colour, uint32_t mode, void *v,
                      int n, uint32_t found_or)
{
    PolyArg arg;
    arg.colour = colour;
    uint32_t m = mode | found_or;
    if (mode & 4) {
        const Texture *t = NULL;
        if (tex[0]) {
            t = Tex_Get((uintptr_t)owner, tex);
            arg.tex = t;
            if (!t) arg.colour = 0;
        }
        if (!t) m = mode & ~0x14u;
    }
    Queue_Poly(view, v, n, arg, (int)m);
}

/* the tree from back to front: each node's far side (by the sign of its plane's d, as bits) first */
static void draw_tree(void)
{
    FaceRec *stack[1024];
    int k = 0;
    FaceRec *at = tree;
    if (!at) return;
    for (;;) {
        do {
            if (k < 1024) stack[k++] = at;
            at = fbits(at->plane[3]) < 0x80000001u ? at->back : at->front;
        } while (at);
        do {
            if (k == 0) return;
            FaceRec *r = stack[--k];
            if (r->n) draw_face(g_view, r->node, r->tex, r->colour, r->mode, r->v, r->n, 0x10);
            at = fbits(r->plane[3]) < 0x80000001u ? r->front : r->back;
        } while (!at);
    }
}

/* 0x48cef0: a class 0x34 object (and type 7): its mesh flat in the heading frame at its position */
static void draw_flat(QRec *rec, const View *view)
{
    Node *n = rec->u.obj.node;
    if (!Lod_Select(n, 0) || !n->geom || n->geom->nverts <= 2 || n->geom->nverts > MAXV) return;
    memcpy(g_heading.pos, n->frame.pos, sizeof g_heading.pos);
    Frame cam;
    Frame_Mul(&cam, &g_heading, &view->cam);
    float (*cv)[3] = (float (*)[3])cvbuf;
    Frame_Points(cv, (const float (*)[3])n->geom->verts, n->geom->nverts, &cam);
    for (const GeomFace *gf = n->geom->faces; gf; gf = gf->next) {
        CamVert v[16];   /* the original's array: more corners overrun it */
        int k = gf->nrefs < 16 ? gf->nrefs : 16;
        for (int i = 0; i < k; i++) {
            memcpy(&v[i].x, cv[gf->refs[i].vertex], 12);
            v[i].u = gf->refs[i].u;
            v[i].v = gf->refs[i].v;
            uint32_t q = 0x3f666666;   /* 0.9 */
            memcpy(&v[i].q, &q, 4);
        }
        uint32_t mode = exe_u32(0x4d6078u + 4u * ((uint32_t)gf->flags[1] + (uint32_t)gf->flags[0] * 8));
        if (gf->flags[2]) mode = (mode & ~0x60u) | 0xa0;
        mode = mode & n->facemask & 0xff;
        draw_face(view, n, gf->texture, gf->colour, mode, v, gf->nrefs, 0);
    }
}

static void merge_members(QMerge *m, const View *view)
{
    for (int j = 0; j < m->n; j++) {
        Node *mn = m->m[j].node;
        if (mn->cls == 0x34) continue;
        Light_Node(mn);
        g_mode_or = 0;
        Lod_Select(mn, g_lod);
        planes(mn, view);
        children(mn, view);
    }
}

static void merge_flats(QMerge *m, const View *view)
{
    for (int j = 0; j < m->n; j++) {
        Node *mn = m->m[j].node;
        if (mn->cls == 0x34) draw_flat(*(QRec **)Data_Ptr(mn->data, 0x14), view);
    }
}

/* 0x434f10: a group's child with children: its own mesh splits, its children are drawn */
static void draw_container(QRec *rec, const View *view)
{
    QObj *o = &rec->u.obj;
    Node *n = o->node;
    if (!n->child) return;
    start(view, o);
    g_mode_or = (uint32_t)o->b;
    Light_Node(n);
    Lod_Select(n, g_lod);
    planes(n, view);
    children(n, view);
    draw_tree();
}

/* 0x4352e0: a node without children (and a merge's members around it); the node itself is not lit
   again: the lights are those of the last node lit */
static void draw_leaf(QRec *rec, const View *view)
{
    QObj *o = &rec->u.obj;
    start(view, o);
    QMerge *m = o->merge;
    if (m) merge_members(m, view);
    g_mode_or = (uint32_t)o->b;
    Node *n = o->node;
    if (!(n->flags & 1) && Lod_Select(n, g_lod) && n->geom && n->geom->nverts) face_records(n, &n->frame, view);
    draw_tree();
    if (m) merge_flats(m, view);
    o->merge = NULL;
}

/* 0x4356f0: an entity's tree (and its merge's members) */
static void draw_entity(QRec *rec, const View *view)
{
    QObj *o = &rec->u.obj;
    Node *n = o->node;
    if (!n->child) return;
    start(view, o);
    g_mode_or = (uint32_t)o->b;
    Light_Node(n);
    Lod_Select(n, g_lod);
    planes(n, view);
    children(n, view);
    QMerge *m = o->merge;
    if (m) merge_members(m, view);
    draw_tree();
    if (m) merge_flats(m, view);
    o->merge = NULL;
}

void Objects_DrawInside(const View *view, Node *n)
{
    if (!n->child) return;
    QObj o = {0};
    o.node = n;
    o.a = 4;   /* the cockpit's level */
    start(view, &o);
    g_mode_or = 0;
    Light_Node(n);
    children(n, view);
    draw_tree();
}

/* the queue's draw functions: 0x48cd20 (type 5), 0x48ccd0 (type 6), 0x48cef0 (type 7) */
static void type5(QRec *r, const View *v)
{
    if (r->u.obj.c) draw_leaf(r, v);
    else if (r->u.obj.ent) draw_entity(r, v);
    else draw_container(r, v);
}
static void type6(QRec *r, const View *v)
{
    if (r->u.obj.c) draw_leaf(r, v);
    else draw_entity(r, v);
}

void Objects_DrawInit(void)
{
    /* types 3 and 0xb (the shadows, 0x48cd60 / 0x4a3c20) and 10 (0x48cd80) are not ported yet */
    Queue_SetDraw(5, type5);
    Queue_SetDraw(6, type6);
    Queue_SetDraw(7, draw_flat);
}

/* ---- Objects_Draw ---- */

/* an entity's level of detail at depth z (scaled by k): with the detail byte 9, the first of its three
   distances b * z * k short of its radius; without, 2 or 3 (3 for the moving ones) */
static int32_t lod_of(const Entity *e, double zk_a, float z, float k, int scaled)
{
    float b[5];
    memcpy(b, e->b, sizeof b);
    if (!e->has_b) return 0;
    if (g_detail.b[9]) {
        int32_t i = 0;
        for (; i < 3; i++) {
            double s = scaled ? MUL(MUL(b[i], z), k) : MUL(b[i], zk_a);
            if (below(s, e->r)) break;
        }
        return i;
    }
    int c = e->node->cls;
    if (c == 1 || c == 7 || c == 9) return 3;
    double s = scaled ? MUL(MUL(b[2], z), k) : MUL(b[2], zk_a);
    return below(s, e->r) ? 2 : 3;
}

static void queue_obj(int type, void *node, const float cp[3], float r, int32_t a, int32_t b, int32_t c, void *ent)
{
    QRec *q = Queue_New();
    q->type = type;
    q->u.obj.node = node;
    q->u.obj.x = cp[0];
    q->u.obj.y = cp[1];
    q->u.obj.z = cp[2];
    q->u.obj.r = r;
    q->u.obj.a = a;
    q->u.obj.b = b;
    q->u.obj.c = c;
    q->u.obj.merge = NULL;
    q->u.obj.ent = ent;
    Queue_Add(F32(SUB(cp[2], r)));
}

void Objects_Draw(const View *view)
{
    Light_Frame(view);
    Entity *target = NULL;
    if (g_cockpit) {   /* 0x4a847c: in the cockpit the camera's entity (0x4a8660) is drawn by the cockpit */
        if (!g_camera_entity) g_camera_entity = g_user_entity;
        target = g_camera_entity;
    }
    float k = F32(DIV(1.0, view->sx));   /* 0x4a58a4 */
    /* the original's lists hold 128 each on its stack; the port's take every entity */
    Entity *stat[MAX_ENTITIES], *mov[MAX_ENTITIES];
    float sc[MAX_ENTITIES][3], mc[MAX_ENTITIES][3];
    int32_t sx[MAX_ENTITIES], mx[MAX_ENTITIES];
    int ns = 0, nm = 0;
    for (int i = 0; i < g_entity_count; i++) {
        Entity *e = &g_entities[i];
        Node *n = e->node;
        if (!n) continue;
        if (n->flags & 0x100) {   /* a group: each child on its own */
            for (Node *c = n->child; c; c = c->next) {
                double w[3];
                Point_ToWorld(w, c->centre, &c->frame);
                float cp[3];
                View_ToCamera(view, cp, w[0], w[1], w[2]);
                int t = View_TestSphere(view, cp, F32(MUL(c->radius, 1.1f)));   /* 0x4a58cc */
                if (t > 0) continue;
                QRec *q = Queue_New();
                c->facemask &= 0xffffffee;
                QObj *o = &q->u.obj;
                o->ent = NULL;
                o->node = c;
                o->b = ~t & 0x100;
                o->x = cp[0];
                o->y = cp[1];
                o->z = cp[2];
                if (c->cls == 0x33) {
                    o->a = 0;
                } else {
                    float zz = F32(MUL(cp[2], k));
                    o->a = lod_of(e, zz, 0, 0, 0);
                }
                o->c = c->child == NULL;
                o->r = c->radius;
                o->merge = NULL;
                q->type = (c->flags & 0x40) ? 7 : 5;
                if (c->cls == 0x34 && c->data && *Data_Ptr(c->data, 0x10)) {   /* an explosion on a node */
                    Queue_AddMerge(c, *Data_Ptr(c->data, 0x10));
                    *Data_Ptr(c->data, 0x14) = q;
                    Queue_Skip();
                } else {
                    Queue_Add(F32(SUB(o->z, o->r)));
                }
            }
            continue;
        }
        if (e == target) continue;
        double w[3];
        Point_ToWorld(w, e->c, &n->frame);
        float cp[3];
        View_ToCamera(view, cp, w[0], w[1], w[2]);
        int t = View_TestSphere(view, cp, F32(MUL(e->r, 1.1f)));
        if (t <= 0) {
            int moving = n->cls == 1 || n->cls == 7 || n->cls == 9;
            if (moving) {
                mov[nm] = e;
                memcpy(mc[nm], cp, sizeof cp);
                mx[nm++] = ~t & 0x100;
            } else {
                stat[ns] = e;
                memcpy(sc[ns], cp, sizeof cp);
                sx[ns++] = ~t & 0x100;
            }
        } else {
            QMerge *m = Queue_FindMerge(n);
            if (m) m->node = NULL;
        }
    }
    /* the shadows within 50 (detail byte 6: 1 always the farthest level) */
    for (int pass = 0; pass < 2; pass++) {
        int cnt = pass ? nm : ns;
        for (int i = 0; i < cnt; i++) {
            Entity *e = pass ? mov[i] : stat[i];
            const float *cp = pass ? mc[i] : sc[i];
            int32_t lod = lod_of(e, 0, cp[2], k, 1);
            if (g_detail.b[DET_SHADOWS] && (int32_t)fbits(cp[2]) < 0x42480000)
                queue_obj(pass ? 3 : 0xb, e, cp, e->r, g_detail.b[DET_SHADOWS] == 1 ? 3 : lod, 0, 0, NULL);
        }
    }
    for (int i = 0; i < ns; i++)
        queue_obj(6, stat[i]->node, sc[i], stat[i]->r, lod_of(stat[i], 0, sc[i][2], k, 1), sx[i], 0, stat[i]);
    /* the followed vehicle's headlight (0x4919e0, its data +0x440) is not ported yet */
    for (int i = 0; i < nm; i++)
        queue_obj(6, mov[i]->node, mc[i], mov[i]->r, lod_of(mov[i], 0, mc[i][2], k, 1), mx[i], 0, mov[i]);
    /* the vehicles' headlights at night (0x4919e0) are not ported yet */
}
