/* The x87 as the rasterizer uses it: 53-bit precision (Windows' default) with a rounding mode the drawing
   entry point (DrawPoly 0x43d730) sets to "up" (towards +infinity) around every filler. WebAssembly has
   only round-to-nearest, so the port computes each operation in nearest and corrects the result with its
   exact error (TwoSum for additions, fma for products and quotients): the same bits as the FPU, on every
   target. The core is built with -ffp-contract=off, so nothing else fuses or reorders. */
#pragma once
#include <math.h>
#include <stdint.h>

enum { FPU_NEAREST, FPU_UP };
extern int g_fpu_round;   /* the current mode (DrawPoly sets FPU_UP for the fillers) */

static inline double fpu_up(double r, double err) { return err > 0 ? nextafter(r, INFINITY) : r; }

static inline double fpu_add(double a, double b)
{
    double s = a + b;
    if (g_fpu_round != FPU_UP) return s;
    double bb = s - a, err = (a - (s - bb)) + (b - bb);
    return fpu_up(s, err);
}
static inline double fpu_sub(double a, double b) { return fpu_add(a, -b); }
static inline double fpu_mul(double a, double b)
{
    double p = a * b;
    return g_fpu_round == FPU_UP ? fpu_up(p, fma(a, b, -p)) : p;
}
static inline double fpu_div(double a, double b)
{
    double q = a / b;
    if (g_fpu_round != FPU_UP) return q;
    double r = fma(-q, b, a);   /* a - q*b, exactly: the true quotient exceeds q when r and b agree in sign */
    return (r > 0) == (b > 0) && r != 0 ? nextafter(q, INFINITY) : q;
}
/* fstp float */
static inline float fpu_f32(double d)
{
    float f = (float)d;
    if (g_fpu_round == FPU_UP && (double)f < d) f = nextafterf(f, INFINITY);
    return f;
}
/* fadd 1.5 * 2^52, then the low dword: to the nearest (ties even), or up. */
static inline int32_t fpu_int(double x) { return (int32_t)(int64_t)(g_fpu_round == FPU_UP ? ceil(x) : nearbyint(x)); }
