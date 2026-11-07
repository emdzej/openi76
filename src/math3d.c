/* The 3D math module (0x452480-0x452a90). */
#include "math3d.h"
#include "exe.h"
#include "render/fpu.h"
#include <math.h>
#include <string.h>

/* in the current rounding mode (render/fpu.h) */
#define ADD fpu_add
#define SUB fpu_sub
#define MUL fpu_mul
#define DIV fpu_div
#define F32 fpu_f32

static int below(double a, double b) { return a < b || a != a || b != b; }

static uint8_t invsqrt_tab[256];   /* 0x5e0e90 */
static int invsqrt_ready;

/* InvSqrt_Init 0x4516f0 (at start-up, rounding to nearest): entry i is the top 8 mantissa bits (rounded) of 1/sqrt(x) for the x in [0.5, 2)
   whose exponent's low bit and top 7 mantissa bits are i */
void InvSqrt_Init(void)
{
    for (int i = 0; i < 256; i++) {
        uint64_t xb = (uint64_t)((uint32_t)(i | 0x1ff00) << 13) << 32;
        double x, r;
        memcpy(&x, &xb, 8);
        r = 1.0 / sqrt(x);
        uint64_t rb;
        memcpy(&rb, &r, 8);
        invsqrt_tab[i] = (uint8_t)(((int32_t)(rb >> 32) + 0x400) >> 12);
    }
    invsqrt_tab[128] = 0xff;
    invsqrt_ready = 1;
}

double Fast_InvSqrt(double x)
{
    if (!invsqrt_ready) InvSqrt_Init();
    uint64_t xb;
    memcpy(&xb, &x, 8);
    uint32_t hi = (uint32_t)(xb >> 32);
    uint32_t gh = ((0xbfcu - (hi >> 20)) >> 1) << 20 | (uint32_t)invsqrt_tab[hi >> 13 & 0xff] << 12;
    uint64_t gb = (uint64_t)gh << 32;
    double g;
    memcpy(&g, &gb, 8);
    double b = MUL(x, MUL(g, g));
    return MUL(MUL(MUL(g, 0.5), SUB(3.0, b)), 1.00001);
}

void Frame_Invert(Frame *out, const Frame *f)
{
    const float *m = f->m;
    float t[9] = {m[0], m[3], m[6], m[1], m[4], m[7], m[2], m[5], m[8]};
    double p[3];
    for (int k = 0; k < 3; k++)
        p[k] = -ADD(ADD(MUL(m[3 * k], f->pos[0]), MUL(m[3 * k + 1], f->pos[1])), MUL(m[3 * k + 2], f->pos[2]));
    memcpy(out->m, t, sizeof t);
    memcpy(out->pos, p, sizeof p);
}

void Plane_From(float out[4], const float a[3], const float b[3], const float c[3])
{
    float ux = F32(SUB(b[0], a[0])), uy = F32(SUB(b[1], a[1])), uz = F32(SUB(b[2], a[2]));
    float vx = F32(SUB(c[0], a[0])), vy = F32(SUB(c[1], a[1]));
    double vzd = SUB(c[2], a[2]);   /* kept on the stack for the first product */
    float vz = F32(vzd);
    float nx = F32(SUB(MUL(vzd, uy), MUL(vy, uz)));
    float ny = F32(SUB(MUL(vx, uz), MUL(vz, ux)));
    float nz = F32(SUB(MUL(vy, ux), MUL(vx, uy)));
    double r = Fast_InvSqrt(ADD(ADD(MUL(ny, ny), MUL(nz, nz)), MUL(nx, nx)));
    float ox = F32(MUL(nx, r)), oy = F32(MUL(ny, r));
    double z = MUL(r, nz);
    out[0] = ox;
    out[1] = oy;
    out[2] = F32(z);
    out[3] = F32(-ADD(ADD(MUL(z, a[2]), MUL(a[1], oy)), MUL(a[0], ox)));
}

void Frame_FromDir(Frame *out, const float p[3], const float d[3])
{
    double inv = Fast_InvSqrt(ADD(ADD(MUL(d[1], d[1]), MUL(d[2], d[2])), MUL(d[0], d[0])));
    float f[3], r[3];
    f[0] = F32(MUL(d[0], inv));
    f[1] = F32(MUL(d[1], inv));
    double z = MUL(inv, d[2]);
    f[2] = F32(z);
    float zz = F32(MUL(z, f[2]));   /* the unrounded z by its float */
    if (below(ADD(MUL(f[0], f[0]), zz), 0.02)) {   /* 0x4a60e0: the direction near vertical */
        r[0] = 1.0f;
        r[1] = r[2] = 0.0f;
    } else {
        /* the horizontal perpendicular, (z, 0, -x) normalised */
        float nx = -f[0];
        double inv2 = Fast_InvSqrt(ADD(MUL(nx, nx), zz));
        r[0] = F32(MUL(f[2], inv2));
        r[1] = 0.0f;
        r[2] = F32(MUL(inv2, nx));
    }
    memset(out, 0, sizeof *out);
    memcpy(out->m, r, sizeof r);
    out->m[3] = F32(SUB(MUL(f[1], r[2]), MUL(f[2], r[1])));
    out->m[4] = F32(SUB(MUL(f[2], r[0]), MUL(f[0], r[2])));
    out->m[5] = F32(SUB(MUL(f[0], r[1]), MUL(f[1], r[0])));
    memcpy(out->m + 6, f, sizeof f);
    for (int k = 0; k < 3; k++) out->pos[k] = p[k];
}

Frame *Frame_Mul(Frame *out, const Frame *a, const Frame *b)
{
    const float *x = a->m, *y = b->m;
    float m[9];
    double p[3];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            m[3 * i + j] = F32(ADD(ADD(MUL(y[j], x[3 * i]), MUL(y[3 + j], x[3 * i + 1])), MUL(y[6 + j], x[3 * i + 2])));
    for (int j = 0; j < 3; j++)
        p[j] = ADD(ADD(ADD(MUL(y[j], a->pos[0]), MUL(y[3 + j], a->pos[1])), MUL(y[6 + j], a->pos[2])), b->pos[j]);
    memcpy(out->m, m, sizeof m);
    memcpy(out->pos, p, sizeof p);
    return out;
}

void Point_ToWorld(double out[3], const float p[3], const Frame *f)
{
    const float *m = f->m;
    double x = ADD(ADD(ADD(MUL(m[6], p[2]), MUL(m[3], p[1])), MUL(m[0], p[0])), f->pos[0]);
    double y = ADD(ADD(ADD(MUL(m[1], p[0]), MUL(m[4], p[1])), MUL(m[7], p[2])), f->pos[1]);
    double z = ADD(ADD(ADD(MUL(m[2], p[0]), MUL(m[8], p[2])), MUL(m[5], p[1])), f->pos[2]);
    out[0] = x;
    out[1] = y;
    out[2] = z;
}

void Frame_Points(float (*out)[3], const float (*in)[3], int n, const Frame *f)
{
    const float *m = f->m;
    for (int i = 0; i < n; i++) {
        const float *v = in[i];
        float r[3];
        for (int j = 0; j < 3; j++)
            r[j] = F32(ADD(ADD(ADD(MUL(m[j], v[0]), MUL(m[3 + j], v[1])), MUL(m[6 + j], v[2])), f->pos[j]));
        memcpy(out[i], r, sizeof r);
    }
}

void Plane_Transform(float out[4], const float p[4], const Frame *f)
{
    const float *m = f->m;
    double n[3];
    for (int j = 0; j < 3; j++) n[j] = ADD(ADD(MUL(m[j], p[0]), MUL(m[3 + j], p[1])), MUL(m[6 + j], p[2]));
    double d = SUB(p[3], ADD(ADD(MUL(n[0], f->pos[0]), MUL(n[1], f->pos[1])), MUL(n[2], f->pos[2])));
    out[0] = F32(n[0]);
    out[1] = F32(n[1]);
    out[2] = F32(n[2]);
    out[3] = F32(d);
}

void Quat_ToFrame(Frame *out, const float q[4])
{
    double x = q[0], y = q[1], z = q[2], w = q[3];
    const float two = 2.0f, one = 1.0f;   /* 0x4a60bc, 0x4a60b8 */
    double xx = MUL(MUL(x, x), two), yy = MUL(MUL(y, y), two), zz = MUL(MUL(z, z), two);
    double xy = MUL(MUL(y, x), two), xz = MUL(MUL(z, x), two), wx = MUL(MUL(w, x), two);
    float zy = F32(MUL(MUL(z, y), two)), wy = F32(MUL(MUL(w, y), two)), zw = F32(MUL(MUL(z, w), two));
    double a = SUB(one, zz);
    float m[9] = {F32(SUB(a, yy)), F32(SUB(xy, zw)), F32(ADD(wy, xz)),
                  F32(ADD(xy, zw)), F32(SUB(a, xx)), F32(SUB(zy, wx)),
                  F32(SUB(xz, wy)), F32(ADD(wx, zy)), F32(SUB(SUB(one, yy), xx))};
    memset(out, 0, sizeof *out);
    memcpy(out->m, m, sizeof m);
}

/* the sine and cosine (fsincos 0x452a70) are the C library's, rounded to nearest before the stores: in the
   other modes the FPU rounds them its way (render.md, "The FPU's rounding") */
Frame *Frame_AxisAngle(Frame *out, float angle, float x, float y, float z)
{
    float h = F32(MUL(angle, 0.5f));   /* 0x4a60cc */
    float s = F32(sin(h)), c = F32(cos(h));
    float q[4] = {F32(MUL(s, x)), F32(MUL(s, y)), F32(MUL(s, z)), c};
    Quat_ToFrame(out, q);
    return out;
}

int Frame_Project(Frame *out, const float plane[4], const float l[3])
{
    const float *n = plane;
    double d = ADD(ADD(MUL(l[1], n[1]), MUL(l[2], n[2])), MUL(l[0], n[0]));
    if (d < 0.1 || d != d) return 0;   /* 0x4a60b0, a double */
    double s = -DIV(1.0, d);
    Frame t, f;
    memcpy(&t, exe_data(0x4db298, sizeof t), sizeof t);
    t.m[0] = F32(MUL(n[0], s));
    t.m[4] = F32(MUL(n[1], s));
    t.m[8] = F32(MUL(n[2], s));
    for (int r = 0; r < 3; r++) memcpy(&f.m[3 * r], l, 3 * sizeof *l);
    f.pad = out->pad;
    for (int k = 0; k < 3; k++) f.pos[k] = l[k];
    Frame_Mul(out, &t, &f);
    double k = MUL(plane[3], s);
    out->pos[0] = MUL(out->pos[0], k);
    out->pos[1] = MUL(out->pos[1], k);
    out->m[0] = F32(ADD(out->m[0], 1.0));   /* 0x4a60b8 */
    out->pos[2] = MUL(k, out->pos[2]);
    out->m[4] = F32(ADD(out->m[4], 1.0));
    out->m[8] = F32(ADD(out->m[8], 1.0));
    return 1;
}
