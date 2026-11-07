/* The x87 as the game leaves it: 53-bit precision (Windows' default) and a rounding mode (the control word's
   RC) that changes as the original changes it:
     - nearest from the start;
     - down from the first terrain height query on (Terrain_Height 0x491f00 sets RC = 1 for its floors and
       never restores it), so every frame and every later mission's load runs rounding down;
     - up inside DrawPoly 0x43d730 around every filler, restored after;
     - down in Terrain_Draw 0x48e8d0 for its quadtree, restored after.
   (docs/re/render.md, "The FPU's rounding".) WebAssembly has only round-to-nearest, so the port computes
   each operation in nearest and corrects the result with its exact error (TwoSum for additions, fma for
   products, quotients and square roots): the same bits as the FPU, in every mode, on every target. The core
   is built with -ffp-contract=off, so nothing else fuses or reorders. Any arithmetic that may run in a
   mode other than nearest goes through these. */
#pragma once
#include <math.h>
#include <stdint.h>

enum { FPU_NEAREST = 0, FPU_DOWN = 1, FPU_UP = 2, FPU_CHOP = 3 };   /* the x87's RC */
extern int g_fpu_round;   /* the current mode */

/* r, the nearest result, moved one step when the exact one (r + err) lies on the far side for the mode */
static inline double fpu_fix(double r, double err)
{
    switch (g_fpu_round) {
    case FPU_UP: return err > 0 ? nextafter(r, INFINITY) : r;
    case FPU_DOWN: return err < 0 ? nextafter(r, -INFINITY) : r;
    case FPU_CHOP: return (r > 0 && err < 0) ? nextafter(r, 0) : (r < 0 && err > 0) ? nextafter(r, 0) : r;
    default: return r;
    }
}

static inline double fpu_add(double a, double b)
{
    double s = a + b;
    if (g_fpu_round == FPU_NEAREST) return s;
    if (s == 0) {
        /* an exact zero: -0 when rounding down, unless both are +0; +0 otherwise, unless both are -0 */
        if (g_fpu_round == FPU_DOWN) return (a == 0 && b == 0 && !signbit(a) && !signbit(b)) ? 0.0 : -0.0;
        return (signbit(a) && signbit(b)) ? -0.0 : 0.0;
    }
    if (isinf(s) || isnan(s)) return s;
    double bb = s - a, err = (a - (s - bb)) + (b - bb);
    return fpu_fix(s, err);
}
static inline double fpu_sub(double a, double b) { return fpu_add(a, -b); }
static inline double fpu_mul(double a, double b)
{
    double p = a * b;
    if (g_fpu_round == FPU_NEAREST || p == 0 || isinf(p) || isnan(p)) return p;
    return fpu_fix(p, fma(a, b, -p));
}
static inline double fpu_div(double a, double b)
{
    double q = a / b;
    if (g_fpu_round == FPU_NEAREST || q == 0 || isinf(q) || isnan(q)) return q;
    double r = fma(-q, b, a);   /* a - q*b, exactly: the true quotient is q + r/b */
    if (r == 0) return q;
    return fpu_fix(q, (r > 0) == (b > 0) ? 1.0 : -1.0);
}
static inline double fpu_sqrt(double a)
{
    double r = sqrt(a);
    if (g_fpu_round == FPU_NEAREST || r == 0 || isinf(r) || isnan(r)) return r;
    return fpu_fix(r, fma(-r, r, a));   /* a - r*r: the true root is above r when positive */
}
/* fstp float */
static inline float fpu_f32(double d)
{
    float f = (float)d;
    if (g_fpu_round == FPU_NEAREST || isnan(d) || (double)f == d) return f;
    switch (g_fpu_round) {
    case FPU_UP: return (double)f < d ? nextafterf(f, INFINITY) : f;
    case FPU_DOWN: return (double)f > d ? nextafterf(f, -INFINITY) : f;
    default: return fabs((double)f) > fabs(d) ? nextafterf(f, 0) : f;
    }
}
/* fistp, or fadd 1.5 * 2^52 and the low dword: the integer in the current mode */
static inline double fpu_round(double x)
{
    switch (g_fpu_round) {
    case FPU_UP: return ceil(x);
    case FPU_DOWN: return floor(x);
    case FPU_CHOP: return trunc(x);
    default: return nearbyint(x);
    }
}
static inline int32_t fpu_int(double x) { return (int32_t)(int64_t)fpu_round(x); }
