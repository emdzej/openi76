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
    /* 0x491650: the objects' road faces, not ported yet */
}
