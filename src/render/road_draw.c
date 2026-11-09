/* The roads' renderer (road_draw.h). In the current rounding mode (render/fpu.h). */
#include "render/road_draw.h"
#include "exe.h"
#include "math3d.h"
#include "roads.h"
#include "settings.h"
#include "render/fpu.h"
#include "render/light.h"
#include "render/queue.h"
#include "render/terrain_draw.h"
#include "render/view.h"
#include "geom.h"
#include "lod.h"
#include "scene.h"
#include "tex.h"
#include <math.h>
#include <stdbool.h>
#include <string.h>

#define ADD fpu_add
#define SUB fpu_sub
#define MUL fpu_mul
#define F32 fpu_f32

static bool below(double a, double b) { return a < b || isnan(a) || isnan(b); }      /* C0 */
static bool below_eq(double a, double b) { return a <= b || isnan(a) || isnan(b); }  /* C0 | C3 */
static int32_t bits(float f) { int32_t b; memcpy(&b, &f, 4); return b; }

/* A segment's four vertices are its points' (k + 1, k, k + 2, k + 3 of the piece's 2n, right, left, next
   right, next left: 0x4eb3a8) with the texture coordinates of 0x4eb370 (0, 0), (1, 0), (1, 1), (0, 1). Its
   texture is 0x4eb430's [type % 3][segment % 10, plus 10 from 60 away]; its mode 0xf4 with the detail
   byte 0x5e14d3, else 0 (flat, in the texture pointer's low byte). */
void Roads_Draw(const View *view)
{
    if (!g_roads_ready || !g_roads) return;
    int mode = g_detail.b[3] ? 0xf4 : 0;
    double l = MUL(Light_Strength(), g_sun[1]);
    float sun = below(l, 0.0) ? 0 : F32(l);
    float amb = Light_Strength2();
    float (*v)[3] = g_terr_vert;   /* the terrain's vertex buffer, 0x58dcb0 */
    const int32_t *ix = (const int32_t *)exe_data(0x4eb3a8, 16);
    const float *uv = (const float *)exe_data(0x4eb370, 48);
    const int32_t *tab = (const int32_t *)exe_data(0x4eb430, 240);
    for (RoadPiece *p = g_roads; p; p = p->next) {
        double dx = SUB(view->centre[0], p->c[0]), dy = SUB(view->centre[1], p->c[1]);
        double dz = SUB(view->centre[2], p->c[2]), r = ADD(p->r, view->radius);
        double d = ADD(ADD(SUB(MUL(dx, dx), MUL(r, r)), MUL(dy, dy)), MUL(dz, dz));
        if (!signbit(d)) continue;
        const int32_t *row = tab + (p->type % 3) * 20;
        int n2 = p->n * 2;
        Frame_Points(v, (const float (*)[3])p->pts, n2, &view->cam);
        for (int k = 0; k < n2 - 2; k += 2) {
            const float *a = v[ix[0] + k], *b = v[ix[1] + k], *c = v[ix[2] + k], *e = v[ix[3] + k];
            float mid[3] = {F32(MUL(ADD(c[0], a[0]), 0.5f)), F32(MUL(ADD(c[1], a[1]), 0.5f))};   /* 0x4a7594 */
            double mz = MUL(ADD(c[2], a[2]), 0.5f);
            mid[2] = F32(mz);
            if (!below_eq(mz, 450.0f)) continue;   /* 0x4a7598 */
            if (View_TestSphere(view, mid, 10.0f) > 0) continue;
            /* facing: the camera on the front of (a, c, b), by a's position on their normal */
            float e1[3], e2[3], nv[3];
            for (int j = 0; j < 3; j++) e1[j] = F32(SUB(c[j], a[j])), e2[j] = F32(SUB(b[j], a[j]));
            nv[0] = F32(SUB(MUL(e2[2], e1[1]), MUL(e1[2], e2[1])));
            nv[1] = F32(SUB(MUL(e1[2], e2[0]), MUL(e1[0], e2[2])));
            nv[2] = F32(SUB(MUL(e1[0], e2[1]), MUL(e1[1], e2[0])));
            double f = ADD(ADD(MUL(a[1], nv[1]), MUL(a[2], nv[2])), MUL(a[0], nv[0]));
            if (below(f, 0.0)) continue;
            QRec *q = Queue_New();
            q->type = 1;
            q->n = 4;
            int t = row[(k >> 1) % 10 + (bits(mid[2]) >= 0x42700000 ? 10 : 0)];
            float g = F32(ADD(MUL(a[2], g_light_k[1]), g_light_k[0]));
            if (below(g, 0.75)) g = 0.75f;   /* 0x4a75a4 */
            if (bits(g) > 0x3f800000) g = 1.0f;
            double sh = ADD(MUL(sun, g), amb);
            float s = below_eq(sh, 1.0) ? F32(sh) : 1.0f;   /* 0x4a75a8 */
            q->mode = mode;
            q->arg.tex = g_road_tex[t];
            const float *src[4] = {a, b, c, e};
            for (int j = 0; j < 4; j++) {
                CamVert *w = &q->u.v[j];
                w->x = src[j][0], w->y = src[j][1], w->z = src[j][2];
                w->u = uv[3 * j], w->v = uv[3 * j + 1];
                w->q = s;
            }
            float z = q->u.v[0].z;
            for (int j = 1; j < 4; j++)
                if (below(SUB(q->u.v[j].z, z), 0.0)) z = q->u.v[j].z;
            q->z = z;
            Queue_Add(z);
        }
    }
    World_DrawFaces(view);   /* 0x491650 */
}

/* 0x491650: the world list's faces (the objects folded into the world: the class 0x51 parts and the
   0x50/0x52/0x53 objects' first children, docs/re/objects.md), as the roads' (type 1, the occluders).
   Each node that is a root (+0x68 0) and not a pick-up (class 5): its centre (through its frame) in
   camera space; with its sphere not wholly outside the view, its level 0 mesh selected, and a mesh of more
   than two points with faces, its points into camera space in the terrain's buffer (0x58dcb0), then each
   face whose plane in camera space has its d's bits below 1 (d <= 0: facing the camera) queued: its n,
   the mode 0xf4 with the detail byte 0x5e14d3 (else 0), its texture by name on the node (Tex_Get), or
   without a name or a texture its colour flat (the mode less 0x14); the vertices' u, v, and a shade by
   the roads' falloff, at its nearest z. With the detail off the record's colour is left as the queue's
   last record had it (the original never sets it; kept). */
void World_DrawFaces(const View *view)
{
    int mode = g_detail.b[3] ? 0xf4 : 0;
    float (*v)[3] = g_terr_vert;
    for (Node *n = g_world_nodes; n; n = n->next) {
        if (n->prev || n->cls == 5) continue;
        double w[3];
        Point_ToWorld(w, n->centre, &n->frame);
        double l = MUL(Light_Strength(), g_sun[1]);
        float sun = below(l, 0.0) ? 0 : F32(l);   /* 0x4a7590 */
        float amb = Light_Strength2();
        float c[3];
        View_ToCamera(view, c, w[0], w[1], w[2]);
        if (View_TestSphere(view, c, n->radius) >= 1) continue;
        if (!Lod_Select(n, 0)) continue;
        Geom *g = n->geom;
        if (!g || g->nverts <= 2 || !g->faces) continue;
        Frame m;
        Frame_Mul(&m, &n->frame, &view->cam);
        Frame_Points(v, (const float (*)[3])g->verts, g->nverts, &m);
        for (GeomFace *f = g->faces; f; f = f->next) {
            float pl[4];
            Plane_Transform(pl, f->plane, &m);
            if (bits(pl[3]) >= 1) continue;
            QRec *q = Queue_New();
            q->type = 1;
            q->n = f->nrefs < 16 ? f->nrefs : 16;   /* the record's room (the port's bound) */
            q->mode = mode;
            if (mode & 4) {
                const Texture *t = f->texture[0] ? Tex_Get((uintptr_t)n, f->texture) : NULL;
                if (t) q->arg.tex = t;
                else {
                    q->arg.colour = f->colour;
                    q->mode &= ~0x14;
                }
            }
            for (int j = 0; j < f->nrefs && j < 16; j++) {
                const float *p = v[f->refs[j].vertex];
                float k = F32(ADD(MUL(p[2], g_light_k[1]), g_light_k[0]));
                if (below(k, 0.75)) k = 0.75f;   /* 0x4a75a4 */
                if (bits(k) > 0x3f800000) k = 1.0f;
                k = F32(ADD(MUL(k, sun), amb));
                if (1.0f < k) k = 1.0f;   /* 0x4a75a8 */
                CamVert *cv = &q->u.v[j];
                cv->x = p[0], cv->y = p[1], cv->z = p[2];
                cv->u = f->refs[j].u, cv->v = f->refs[j].v;
                cv->q = k;
            }
            float z = q->u.v[0].z;
            for (int j = 1; j < f->nrefs && j < 16; j++)
                if (below(SUB(q->u.v[j].z, z), 0.0)) z = q->u.v[j].z;
            q->z = z;
            Queue_Add(z);
        }
    }
}
