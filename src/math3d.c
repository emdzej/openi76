/* The 3D math module (0x452480-0x452a90). */
#include "math3d.h"
#include <math.h>
#include <string.h>

static float f32(double d) { return (float)d; }

static uint8_t invsqrt_tab[256];   /* 0x5e0e90 */
static int invsqrt_ready;

/* InvSqrt_Init 0x4516f0: entry i is the top 8 mantissa bits (rounded) of 1/sqrt(x) for the x in [0.5, 2)
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
    double b = x * (g * g);
    return g * 0.5 * (3.0 - b) * 1.00001;
}

void Frame_Invert(Frame *out, const Frame *f)
{
    const float *m = f->m;
    float t[9] = {m[0], m[3], m[6], m[1], m[4], m[7], m[2], m[5], m[8]};
    double p[3];
    for (int k = 0; k < 3; k++)
        p[k] = -(m[3 * k] * f->pos[0] + m[3 * k + 1] * f->pos[1] + m[3 * k + 2] * f->pos[2]);
    memcpy(out->m, t, sizeof t);
    memcpy(out->pos, p, sizeof p);
}

void Plane_From(float out[4], const float a[3], const float b[3], const float c[3])
{
    float ux = f32((double)b[0] - a[0]), uy = f32((double)b[1] - a[1]), uz = f32((double)b[2] - a[2]);
    float vx = f32((double)c[0] - a[0]), vy = f32((double)c[1] - a[1]);
    double vzd = (double)c[2] - a[2];   /* kept on the stack for the first product */
    float vz = f32(vzd);
    float nx = f32(vzd * uy - (double)vy * uz);
    float ny = f32((double)vx * uz - (double)vz * ux);
    float nz = f32((double)vy * ux - (double)vx * uy);
    double r = Fast_InvSqrt(((double)ny * ny + (double)nz * nz) + (double)nx * nx);
    float ox = f32(nx * r), oy = f32(ny * r);
    double z = r * nz;
    out[0] = ox;
    out[1] = oy;
    out[2] = f32(z);
    out[3] = f32(-((z * a[2] + (double)a[1] * oy) + (double)a[0] * ox));
}

Frame *Frame_Mul(Frame *out, const Frame *a, const Frame *b)
{
    const float *x = a->m, *y = b->m;
    float m[9];
    double p[3];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            m[3 * i + j] = f32(((double)y[j] * x[3 * i] + (double)y[3 + j] * x[3 * i + 1]) + (double)y[6 + j] * x[3 * i + 2]);
    for (int j = 0; j < 3; j++)
        p[j] = (((double)y[j] * a->pos[0] + (double)y[3 + j] * a->pos[1]) + (double)y[6 + j] * a->pos[2]) + b->pos[j];
    memcpy(out->m, m, sizeof m);
    memcpy(out->pos, p, sizeof p);
    return out;
}

void Frame_Points(float (*out)[3], const float (*in)[3], int n, const Frame *f)
{
    const float *m = f->m;
    for (int i = 0; i < n; i++) {
        const float *v = in[i];
        float r[3];
        for (int j = 0; j < 3; j++)
            r[j] = f32((((double)m[j] * v[0] + (double)m[3 + j] * v[1]) + (double)m[6 + j] * v[2]) + f->pos[j]);
        memcpy(out[i], r, sizeof r);
    }
}

void Plane_Transform(float out[4], const float p[4], const Frame *f)
{
    const float *m = f->m;
    double n[3];
    for (int j = 0; j < 3; j++) n[j] = ((double)m[j] * p[0] + (double)m[3 + j] * p[1]) + (double)m[6 + j] * p[2];
    double d = p[3] - ((n[0] * f->pos[0] + n[1] * f->pos[1]) + n[2] * f->pos[2]);
    out[0] = f32(n[0]);
    out[1] = f32(n[1]);
    out[2] = f32(n[2]);
    out[3] = f32(d);
}
