/* The terrain's renderer (terrain_draw.h). In the current rounding mode (render/fpu.h): in the game,
   rounding down. */
#include "render/terrain_draw.h"
#include "math3d.h"
#include "terrain.h"
#include "render/fpu.h"
#include <math.h>
#include <stdbool.h>
#include <string.h>

#define ADD fpu_add
#define SUB fpu_sub
#define MUL fpu_mul
#define DIV fpu_div
#define F32 fpu_f32

int32_t g_terr_x0, g_terr_z0, g_terr_depth, g_terr_nedges;
TerrEdge g_terr_edge[6];

static bool below(double a, double b) { return a < b || isnan(a) || isnan(b); }      /* C0 */
static bool below_eq(double a, double b) { return a <= b || isnan(a) || isnan(b); }  /* C0 | C3 */
/* fadd 1.5 * 2^52 and the low dword */
static int32_t magic(double d) { return isnan(d) ? 0 : (int32_t)(uint32_t)(uint64_t)(int64_t)fpu_round(d); }

void Terr_Plane(TerrEdge *out, const double p[3], const double q[3], const double r[3])
{
    double ux = SUB(q[0], p[0]), uy = SUB(q[1], p[1]), uz = SUB(q[2], p[2]);
    double vx = SUB(r[0], p[0]), vy = SUB(r[1], p[1]), vz = SUB(r[2], p[2]);
    double n0 = SUB(MUL(vz, uy), MUL(vy, uz));
    double n1 = SUB(MUL(vx, uz), MUL(vz, ux));
    double n2 = SUB(MUL(vy, ux), MUL(vx, uy));
    double k = Fast_InvSqrt(ADD(ADD(MUL(n2, n2), MUL(n1, n1)), MUL(n0, n0)));
    float a = F32(MUL(n0, k)), b = F32(MUL(n1, k)), c = F32(MUL(k, n2));
    out->a = a, out->b = b, out->c = c;
    uint64_t vyb;
    memcpy(&vyb, &vy, 8);
    out->junk = (uint32_t)(vyb >> 32);
    out->d = SUB(SUB(MUL(-(double)a, p[0]), MUL(p[1], b)), MUL(p[2], c));
}

double Terr_Key(const double a[3], const double b[3])
{
    double dx = SUB(b[0], a[0]), dz = SUB(b[2], a[2]);
    double d2 = ADD(MUL(dz, dz), MUL(dx, dx));
    /* fpatan: the C library's, to nearest (render.md, "The FPU's rounding") */
    double ang = atan2(dz, dx);
    if (below(ang, 0.0)) ang = ADD(ang, 6.283185307179586);   /* 0x4a74f0 */
    return ADD(MUL(ang, 10000000.0), d2);                       /* 0x4a74f8 */
}

typedef struct { double x, y, z; } P3;

int Terrain_Visible(const View *v)
{
    const double *eye = v->eye;
    double h = Terrain_Height(eye[0], eye[2]);   /* from here on, rounding down */
    if (below(SUB(eye[1], h), 0.01)) return 0;   /* 0x4a7520 */
    double k = g_inv_cell;
    P3 pt[5];
    pt[0] = (P3){MUL(k, eye[0]), 0, MUL(eye[2], k)};
    int32_t maxx, maxz, minx, minz;
#define BOUNDS_INIT() do { \
        int32_t ix = magic(pt[0].x), iz = magic(pt[0].z); \
        maxx = ix > -0xc00 ? ix : -0xc00; maxz = iz > -0xc00 ? iz : -0xc00; \
        minx = ix < 0x3400 ? ix : 0x3400; minz = iz < 0x3400 ? iz : 0x3400; \
    } while (0)
#define BOUNDS(p) do { \
        int32_t ix = magic((p).x), iz = magic((p).z); \
        if (ix > maxx) maxx = ix; \
        if (iz > maxz) maxz = iz; \
        if (ix < minx) minx = ix; \
        if (iz < minz) minz = iz; \
    } while (0)
    BOUNDS_INIT();
    int all_below = 1;
    for (int i = 0; i < 4; i++) {
        const double *c = v->corner[i];
        pt[1 + i] = (P3){MUL(c[0], k), 0, MUL(c[2], k)};
        BOUNDS(pt[1 + i]);
        if (below(0.0, c[1])) all_below = 0;
    }
    if (all_below) {
        /* every far corner below the ground plane: each moved to where its ray meets y = 0 */
        BOUNDS_INIT();
        for (int i = 0; i < 4; i++) {
            const double *c = v->corner[i];
            double t = MUL(DIV(c[1], SUB(eye[1], c[1])), k);
            pt[1 + i].x = ADD(MUL(SUB(c[0], eye[0]), t), pt[1 + i].x);
            pt[1 + i].z = ADD(MUL(SUB(c[2], eye[2]), t), pt[1 + i].z);
            BOUNDS(pt[1 + i]);
        }
    }
#undef BOUNDS
#undef BOUNDS_INIT
    minx = (int32_t)((uint32_t)minx & ~31u) - 0x20;
    minz = (int32_t)((uint32_t)minz & ~31u) - 0x20;
    g_terr_x0 = minx, g_terr_z0 = minz;
    int32_t ext = maxx - minx;
    if (ext < maxz - minz) ext = maxz - minz;
    int32_t depth = 0;
    for (; ext != 0 && ext != -1; ext >>= 1) depth++;   /* the original's sar never ends for a negative extent */
    g_terr_depth = depth;

    /* the points with none of the later ones within half a cell */
    P3 q[6];
    int n = 0;
    for (int i = 0; i < 5; i++) {
        int keep = 1;
        for (int j = i + 1; j < 5; j++) {
            double dx = SUB(pt[i].x, pt[j].x), dz = SUB(pt[i].z, pt[j].z);
            if (below(ADD(MUL(dz, dz), MUL(dx, dx)), 0.25)) {   /* 0x4a7528 */
                keep = 0;
                break;
            }
        }
        if (keep) q[n++] = pt[i];
    }
    if (n < 3) return 0;

    /* the hull, gift-wrapped from the point of least z (the last of equals), by Terr_Key */
    double best = 1e10;
    int idx = 0;
    for (int i = 0; i < n; i++)
        if (below_eq(q[i].z, best)) best = q[i].z, idx = i;
    q[n] = q[idx];
    best = 0;
    int hn = 0;
    do {
        P3 t = q[hn];
        q[hn] = q[idx];
        q[idx] = t;
        double last = best;
        idx = n;
        hn++;
        best = 125663706.14359173;   /* 4e7 pi */
        for (int j = hn; j < n + 1; j++) {
            double key = Terr_Key(&q[hn - 1].x, &q[j].x);
            if (!below_eq(key, last) && below(key, best)) best = key, idx = j;
        }
    } while (idx != n);   /* ends at the sentinel: at hn = n only it is left */

    double sx = 0, sz = 0;
    for (int i = 0; i < hn; i++) {
        sx = ADD(sx, q[i].x);
        sz = ADD(sz, q[i].z);
    }
    double inv = DIV(1.0f, hn);   /* 0x4a7510 / n, kept unrounded */
    sx = MUL(sx, inv);
    sz = MUL(inv, sz);
    for (int i = 0, prev = hn - 1; i < hn; prev = i, i++) {
        double up[3] = {q[i].x, 10.0, q[i].z};
        TerrEdge *e = &g_terr_edge[i];
        Terr_Plane(e, &q[i].x, &q[prev].x, up);
        /* turned to face the centroid */
        if (below(ADD(ADD(MUL(e->c, sz), MUL(e->a, sx)), e->d), 0.0)) {
            e->a = -e->a;
            e->c = -e->c;
            e->d = -e->d;
        }
    }
    g_terr_nedges = hn;
    return 1;
}
