/* The view: projection, frustum and camera (0x43c8a0-0x43d36a), and the rectangle helpers. All of it runs in
   the FPU's default rounding with 53-bit precision, which is plain double arithmetic; the operation order
   is the original's, and f32() marks each store to a float. */
#include "render/view.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

static float f32(double d) { return (float)d; }
static int32_t bits(float f) { int32_t i; memcpy(&i, &f, 4); return i; }
/* the original clamps floats by comparing their bits as signed integers */
static float clamp_bits(float f, int32_t lo, int32_t hi)
{
    if (bits(f) < lo) memcpy(&f, &lo, 4);
    if (bits(f) > hi) memcpy(&f, &hi, 4);
    return f;
}
/* __ftol 0x496690: truncation, the low dword of the 64-bit result */
static int ftol(float f) { return (int)(uint32_t)(int64_t)truncf(f); }

/* the outcode of (x, y) against c: bit 31 left, 30 right, 29 above, 28 below, from the sign bits of the
   (wrapping) differences */
static uint32_t outcode(int32_t x, int32_t y, const int c[4])
{
    uint32_t sx0 = (uint32_t)x - (uint32_t)c[0], sx1 = (uint32_t)x - (uint32_t)c[2] - 1;
    uint32_t sy0 = (uint32_t)y - (uint32_t)c[1], sy1 = (uint32_t)y - (uint32_t)c[3] - 1;
    return ((sx0 >> 31) << 31 | (~sx1 >> 31) << 30 | (sy0 >> 31) << 29 | (~sy1 >> 31) << 28);
}

int Rect_Clip(int r[4], const int c[4])
{
    uint32_t a = outcode(r[0], r[1], c), b = outcode(r[2], r[3], c);
    if (a & b) return -1;
    if (!(a | b)) return 0;
    if (a & 0x80000000u) r[0] = c[0];
    if (a & 0x20000000u) r[1] = c[1];
    if (b & 0x40000000u) r[2] = c[2];
    if (b & 0x10000000u) r[3] = c[3];
    return 1;
}

void Surface_SetViewport(Surface *s, int x0, int y0, int x1, int y1)
{
    int r[4] = {x1 < x0 ? x1 : x0, y1 < y0 ? y1 : y0, x1 < x0 ? x0 : x1, y1 < y0 ? y0 : y1};
    int c[4] = {0, 0, s->pitch - 1, s->h - 1};   /* +0 the width (the pitch), +4 the height */
    if (Rect_Clip(r, c) < 0) return;
    s->x0 = r[0];
    s->y0 = r[1];
    s->x1 = r[2];
    s->y1 = r[3];
}

void View_SetRect(View *v, int x0, int y0, int x1, int y1)
{
    Surface *s = v->surface;
    Surface_SetViewport(s, x0, y0, x1, y1);
    v->xmin = (float)s->x0;
    v->ymin = (float)s->y0;
    v->xmax = (float)s->x1;
    v->ymax = (float)s->y1;
    const float tenth = 0.1f;
    double kx = tenth / (double)v->sx, ky = tenth / (double)v->sy;
    double l = (double)v->xmin - v->cx, t = (double)v->ymin - v->cy;
    float rf = f32((double)v->xmax - v->cx), bf = f32((double)v->ymax - v->cy);
    float lf = f32(l), tf = f32(t);
    float nl = f32(l * kx), nt = f32(t * ky), nr = f32(kx * rf), nb = f32(ky * bf);
    float near[4][3] = {{nl, nt, tenth}, {nr, nt, tenth}, {nr, nb, tenth}, {nl, nb, tenth}};
    double zr = (double)v->range * v->zoom;
    float z = !(zr >= 1500) ? f32(zr) : 1500.0f;
    double fx = z / (double)v->sx, fy = z / (double)v->sy;
    float fl = f32(lf * fx), ft = f32(tf * fy), fr = f32(fx * rf), fb = f32(fy * bf);
    float far[4][3] = {{fl, ft, z}, {fr, ft, z}, {fr, fb, z}, {fl, fb, z}};
    memcpy(v->near_c, near, sizeof near);
    memcpy(v->far_c, far, sizeof far);

    static const float o[3];
    float (*fc)[3] = v->far_c, (*nc)[3] = v->near_c;
    Plane_From(v->plane[0], o, fc[0], fc[3]);
    Plane_From(v->plane[1], o, fc[1], fc[2]);
    Plane_From(v->plane[2], o, fc[0], fc[1]);
    Plane_From(v->plane[3], o, fc[2], fc[3]);
    Plane_From(v->plane[4], fc[0], fc[1], fc[2]);
    Plane_From(v->plane[5], nc[0], nc[1], nc[2]);

    /* a point inside: the mean of the apex and the far corners; every plane is turned to face it */
    float m[3];
    for (int k = 0; k < 3; k++) {
        double sum = 0;
        for (int i = 0; i < 4; i++) sum += fc[i][k];
        m[k] = f32((double)f32(sum) * 0.2f);
    }
    for (int i = 0; i < 6; i++) {
        float *p = v->plane[i];
        double d = ((double)p[0] * m[0] + (double)p[1] * m[1]) + (double)p[2] * m[2] + p[3];
        if (!(d >= 0.0))
            for (int k = 0; k < 4; k++) p[k] = -p[k];
    }
}

void View_SetZoom(View *v, float zoom)
{
    zoom = clamp_bits(zoom, 0x3f000000, 0x41000000);   /* 0.5-8 */
    v->zoom = zoom;
    double tn = v->tan_half;
    double w = (double)v->surface->pitch * zoom * 0.5;
    double far = (double)v->range * zoom, k = (double)zoom * 600.0f;
    double sec2 = tn * tn + 1.0f;
    v->sx = f32(w / tn);
    v->sy = f32(-(w * ((double)v->aspect / tn)));
    v->far_radius = f32(sqrt(sec2 * (far * far)));
    v->radius600 = f32(sqrt(sec2 * (k * k)));
    View_SetRect(v, ftol(v->xmin), ftol(v->ymin), ftol(v->xmax), ftol(v->ymax));
}

void View_SetCamera(View *v, const Frame *f)
{
    const float *m = f->m;
    const double *p = f->pos;
    double lo[3], hi[3];
    for (int k = 0; k < 3; k++) v->eye[k] = lo[k] = hi[k] = p[k];
    for (int i = 0; i < 4; i++) {
        const float *c = v->far_c[i];
        double *w = v->corner[i];
        w[0] = ((double)m[0] * c[0] + (double)c[1] * m[3]) + (double)c[2] * m[6] + p[0];
        w[1] = ((double)c[0] * m[1] + (double)c[2] * m[7]) + (double)c[1] * m[4] + p[1];
        w[2] = ((double)c[1] * m[5] + (double)c[0] * m[2]) + (double)c[2] * m[8] + p[2];
        for (int k = 0; k < 3; k++) if (!(w[k] >= lo[k])) lo[k] = w[k];
        for (int k = 0; k < 3; k++) if (w[k] > hi[k]) hi[k] = w[k];
    }
    for (int k = 0; k < 3; k++) v->centre[k] = (hi[k] + lo[k]) * 0.5;
    double r2 = 0;
    const double *pts[5] = {v->eye, v->corner[0], v->corner[1], v->corner[2], v->corner[3]};
    for (int i = 0; i < 5; i++) {
        double dx = v->centre[0] - pts[i][0], dy = v->centre[1] - pts[i][1], dz = v->centre[2] - pts[i][2];
        double d = (dx * dx + dz * dz) + dy * dy;
        if (d > r2) r2 = d;
    }
    v->radius = sqrt(r2);
    Frame_Invert(&v->cam, f);
}

void View_ToCamera(const View *v, float out[3], double x, double y, double z)
{
    const float *m = v->cam.m;
    const double *p = v->cam.pos;
    float a = f32(((m[3] * y + m[6] * z) + m[0] * x) + p[0]);
    float b = f32(((m[1] * x + m[7] * z) + m[4] * y) + p[1]);
    float c = f32(((m[5] * y + m[2] * x) + m[8] * z) + p[2]);
    out[0] = a;
    out[1] = b;
    out[2] = c;
}

/* a sphere against a plane, d its centre's distance: bit 0 when its near side is behind (d - r < 0), bit 1
   when its far side is too (from the sign bits of the stored floats) */
static int side(double d, float r)
{
    return (signbit(f32(d - r)) ? 1 : 0) | (signbit(f32(r + d)) ? 2 : 0);
}

int View_TestSphere(const View *v, const float c[3], float r)
{
    /* the near and far planes by z alone */
    int near = side((double)v->plane[5][2] * c[2] + v->plane[5][3], r);
    if (near == 3) return 1;
    int far = side((double)v->plane[4][2] * c[2] + v->plane[4][3], r);
    if (far == 3) return 1;
    int acc = near | far;
    float rr = f32((double)r * 1.1f);   /* the sides, through the apex, with a margin */
    for (int i = 0; i < 4; i++) {
        const float *p = v->plane[i];
        int code = side(((double)p[0] * c[0] + (double)p[2] * c[2]) + (double)p[1] * c[1], rr);
        if (code == 3) return 1;
        acc |= code;
    }
    return -acc;
}

void View_Create(View *v, Surface *s, float fov, float aspect, float range, float zoom)
{
    memset(v, 0, sizeof *v);
    v->surface = s;
    fov = clamp_bits(fov, 0x3f490fdb, 0x4016cbe4);    /* pi/4 - 3pi/4 */
    range = clamp_bits(range, 0x42c80000, 0x447a0000); /* 100 - 1000 */
    float half = f32((double)fov * 0.5f);
    float sn = f32(sin(half)), cs = f32(cos(half));    /* fsincos, 0x452a70 */
    v->cx = f32(s->pitch * 0.5);
    v->cy = f32(s->h * 0.5);
    v->aspect = aspect;
    v->range = range;
    v->xmin = (float)s->x0;
    v->ymin = (float)s->y0;
    v->xmax = (float)s->x1;
    v->ymax = (float)s->y1;
    v->tan_half = f32((double)sn / cs);
    View_SetZoom(v, zoom);
    Frame id = {{1, 0, 0, 0, 1, 0, 0, 0, 1}, 0, {0, 1, 0}};   /* 0x4db298, then the position */
    View_SetCamera(v, &id);
}
