/* The clippers are Sutherland-Hodgman passes, one per plane the polygon crosses, into one local buffer that
   each pass appends to. They run in the FPU's default rounding (to nearest); the fpu.h operations are plain
   double arithmetic then. */
#include "render/clip.h"
#include "render/fpu.h"
#include <string.h>

#define ADD fpu_add
#define SUB fpu_sub
#define MUL fpu_mul
#define DIV fpu_div
#define F32 fpu_f32

typedef float V6[6];   /* either vertex kind: six floats */

/* the x87's "below" (C0) after comparing a with b: less, or unordered */
static int below(float a, float b) { return !(a >= b); }

enum { BUF = 72 };   /* the clippers' local buffers (0x6c0 bytes) */

/* One pass against the plane `axis` = p. A min plane keeps a >= p, a max plane a < p. rev: the previous vertex
   is compared the other way round (fld p; fcomp a), which only differs for NaN. An intersection gets p
   itself on the axis and the listed fields interpolated, prev + (cur - prev) * t; its other fields keep
   what the buffer held. Returns the count appended at out. */
static int clip_pass(V6 *in, int n, V6 *out, int room, int axis, float p, int max, int rev, const int *f, int nf)
{
    int k = 0;
    const float *prev = in[n - 1];
    for (int i = 0; i < n; i++) {
        const float *cur = in[i];
        int cin = max ? below(cur[axis], p) : !below(cur[axis], p);
        int pin = rev ? (max ? prev[axis] < p : !(prev[axis] < p)) : (max ? below(prev[axis], p) : !below(prev[axis], p));
        if (cin != pin && k < room) {   /* the crossing first, then cur if it is in */
            double t = DIV(SUB(p, prev[axis]), SUB(cur[axis], prev[axis]));
            float *x = out[k++];
            x[axis] = p;
            for (int j = 0; j < nf; j++) x[f[j]] = F32(ADD(MUL(SUB(cur[f[j]], prev[f[j]]), t), prev[f[j]]));
        }
        if (cin && k < room) memcpy(out[k++], cur, sizeof(V6));
        prev = cur;
    }
    return k;
}

/* the 2D clippers: outcode 8 x < xmin, 4 x >= xmax, 2 y < ymin, 1 y >= ymax; passes in the order 8, 2, 4, 1 */
static int clip_screen(const View *view, ScreenVert *out, const ScreenVert *in, int n, int tex)
{
    static const int fx[] = {1, 2, 5, 3, 4}, fy[] = {0, 2, 5, 3, 4};   /* the other axis, w, q (u, v) */
    int nf = tex ? 5 : 3, all = 0xff, any = 0;
    for (int i = 0; i < n; i++) {
        int c = below(in[i].x, view->xmin) << 3 | !below(in[i].x, view->xmax) << 2 |
                below(in[i].y, view->ymin) << 1 | !below(in[i].y, view->ymax);
        all &= c, any |= c;
    }
    if (all) return 0;
    if (!any) {
        memcpy(out, in, (size_t)n * sizeof *out);
        return n;
    }
    V6 buf[BUF];
    memset(buf, 0, sizeof buf);   /* the original's stack: what intersections do not set */
    V6 *cur = (V6 *)(void *)in;
    int used = 0;
    /* the textured clipper compares the previous vertex reversed on the x planes, the untextured one on the
       bottom plane */
    const struct { int bit, axis, max, rev_tex, rev_untex; float p; } pass[] = {
        {8, 0, 0, 1, 0, view->xmin}, {2, 1, 0, 0, 0, view->ymin},
        {4, 0, 1, 1, 0, view->xmax}, {1, 1, 1, 0, 1, view->ymax},
    };
    for (int k = 0; k < 4; k++) {
        if (!(any & pass[k].bit)) continue;
        int m = clip_pass(cur, n, buf + used, BUF - used, pass[k].axis, pass[k].p, pass[k].max,
                          tex ? pass[k].rev_tex : pass[k].rev_untex, pass[k].axis ? fy : fx, nf);
        if (!m) return 0;
        cur = buf + used, used += m, n = m;
    }
    memcpy(out, cur, (size_t)n * sizeof *out);
    return n;
}

/* Clip_Screen 0x440350 */
int Clip_Screen(const View *view, ScreenVert *out, const ScreenVert *in, int n, int mode) { return clip_screen(view, out, in, n, 0); }
/* Clip_ScreenTex 0x43fbf0 */
int Clip_ScreenTex(const View *view, ScreenVert *out, const ScreenVert *in, int n, int mode) { return clip_screen(view, out, in, n, 1); }

static void project_xy(const View *view, ScreenVert *o, const float *c, double w)
{
    o->x = F32(ADD(MUL(MUL(c[0], view->sx), w), view->cx));
    o->y = F32(ADD(MUL(MUL(c[1], view->sy), w), view->cy));
}

/* back to affine u, v (mode bit 4 clear): times 1 / w again */
static void unperspective(ScreenVert *o, int n)
{
    for (int i = 0; i < n; i++) {
        double r = DIV(1.0, o[i].w);
        o[i].u = F32(MUL(o[i].u, r)), o[i].v = F32(MUL(r, o[i].v));
    }
}

/* Proj_Untextured 0x4412e0: u, v not written */
int Proj_Untextured(const View *view, ScreenVert *out, const CamVert *in, int n, int mode)
{
    for (int i = 0; i < n; i++) {
        double w = DIV(1.0, in[i].z);
        project_xy(view, &out[i], &in[i].x, w);
        out[i].w = F32(w), out[i].q = in[i].q;
    }
    return n;
}

/* Proj_Textured 0x441340 (its two loops, for mode bit 1 set and clear, are the same code) */
int Proj_Textured(const View *view, ScreenVert *out, const CamVert *in, int n, int mode)
{
    for (int i = 0; i < n; i++) {
        double w = DIV(1.0, in[i].z);
        project_xy(view, &out[i], &in[i].x, w);
        out[i].w = F32(w), out[i].u = F32(MUL(in[i].u, w)), out[i].v = F32(MUL(w, in[i].v)), out[i].q = in[i].q;
    }
    if (!(mode & 0x10)) unperspective(out, n);
    return n;
}

/* the near and far passes: outcode 2 z < near, 1 z >= far. Returns the count and where the result is. */
static int clip_z(const View *view, CamVert *in, int n, V6 *buf, int tex, V6 **res)
{
    static const int f[] = {0, 1, 5, 3, 4};   /* x, y, q (u, v) */
    int all = 0xff, any = 0;
    for (int i = 0; i < n; i++) {
        int c = below(in[i].z, view->znear) << 1 | !below(in[i].z, view->zfar);
        all &= c, any |= c;
    }
    *res = (V6 *)(void *)in;
    if (all) return 0;
    int used = 0;
    if (any & 2) {
        n = clip_pass(*res, n, buf, BUF, 2, view->znear, 0, 0, f, tex ? 5 : 3);
        if (!n) return 0;
        *res = buf, used = n;
    }
    if (any & 1) {   /* appended after the near pass's output */
        n = clip_pass(*res, n, buf + used, BUF - used, 2, view->zfar, 1, 0, f, tex ? 5 : 3);
        if (!n) return 0;
        *res = buf + used;
    }
    return n;
}

/* Clip_Camera 0x4409c0 */
int Clip_Camera(const View *view, ScreenVert *out, const CamVert *in, int n, int mode)
{
    V6 buf[BUF];
    memset(buf, 0, sizeof buf);
    V6 *z;
    n = clip_z(view, (CamVert *)in, n, buf, 0, &z);
    if (!n) return 0;
    /* projected into the buffer's start, behind (or at) the read position */
    ScreenVert *p = (ScreenVert *)(void *)buf;
    for (int i = 0; i < n; i++) {
        CamVert c;
        memcpy(&c, z[i], sizeof c);
        double w = DIV(1.0, c.z);
        project_xy(view, &p[i], &c.x, w);
        p[i].w = F32(w), p[i].q = c.q;
    }
    return clip_screen(view, out, p, n, 0);
}

/* the u, v clamp, on the bits: below zero (but -0) to 0, above one to one */
static void clamp01(float *f)
{
    uint32_t b;
    memcpy(&b, f, 4);
    if (b > 0x80000000u) b = 0;
    if ((int32_t)b > 0x3f800000) b = 0x3f800000;
    memcpy(f, &b, 4);
}

/* Clip_CameraTex 0x440d90 */
int Clip_CameraTex(const View *view, ScreenVert *out, CamVert *in, int n, int mode)
{
    V6 buf[BUF];
    memset(buf, 0, sizeof buf);
    V6 *z;
    n = clip_z(view, in, n, buf, 1, &z);
    if (!n) return 0;
    ScreenVert p[BUF];
    memset(p, 0, sizeof p);
    for (int i = 0; i < n; i++) {
        float *c = z[i];
        if (mode & 2) {
            double w = DIV(1.0, c[2]);
            project_xy(view, &p[i], c, w);
            p[i].w = F32(w), p[i].u = F32(MUL(c[3], w)), p[i].v = F32(MUL(w, c[4]));
        } else {   /* untiled: u, v clamped where they are, and 1 / z rounded to a float first */
            float w = F32(DIV(1.0, c[2]));
            project_xy(view, &p[i], c, w);
            p[i].w = w;
            clamp01(&c[3]), clamp01(&c[4]);
            p[i].u = F32(MUL(c[3], w)), p[i].v = F32(MUL(c[4], w));
        }
        memcpy(&p[i].q, &c[5], 4);
    }
    n = clip_screen(view, out, p, n, 1);
    if (!(mode & 0x10) && n > 0) unperspective(out, n);
    return n;
}
