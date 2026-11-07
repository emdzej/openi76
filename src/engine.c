/* The engine and gearbox (engine.h). In the current rounding mode (render/fpu.h). The comparisons the
   original makes on a float's bits as a signed integer are kept as such (bits()). */
#include "engine.h"
#include "clock.h"
#include "exe.h"
#include "render/fpu.h"
#include <math.h>
#include <string.h>

#define ADD fpu_add
#define SUB fpu_sub
#define MUL fpu_mul
#define F32 fpu_f32

static int32_t bits(float f) { int32_t b; memcpy(&b, &f, 4); return b; }
static float rdf(const uint8_t *p) { float v; memcpy(&v, p, 4); return v; }
static void putf(uint8_t *p, float v) { memcpy(p, &v, 4); }
static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }
static void put32(uint8_t *p, int32_t v) { memcpy(p, &v, 4); }
static int below(double a, double b) { return a < b || a != a || b != b; }

/* the throttle against the line through (lo, 0) with a slope of k1 k2 * 0.875: 1 below it, 0 above, -1
   outside thr in [tlo, 0.875], s in [slo, shi] (the bounds as bits) */
static int shift_line(float thr, float s, int32_t tlo, int32_t slo, int32_t shi, float lo, uint32_t k1,
                      uint32_t k2)
{
    if (bits(thr) < tlo || bits(thr) > 0x3f600000 || bits(s) < slo || bits(s) > shi) return -1;
    return below(MUL(MUL(MUL(SUB(s, lo), exe_f32(k1)), exe_f32(k2)), 0.875f), thr);   /* 0x4a5b24 */
}

int Gear_Choose(float thr, float spd, int g, int mode)
{
    float s = F32(fabs(MUL(spd, 3.6f)));   /* 0x4a5b14: km/h */
    if (mode == 5 && g > 2) return 2;
    if (mode == 4 && g > 3) return 3;
    switch (g) {
    case 2:   /* down to first below 0.5 km/h with the throttle off */
        return bits(thr) < 0x3b03126f && bits(s) < 0x3f000000 ? 1 : g;
    case 3:   /* down to second: full throttle below 60, or below the line from 60 between 12 and 60 */
        if (bits(thr) > 0x3f600000 && bits(s) < 0x42700000) return 2;
        if (shift_line(thr, s, 0x3ea00000, 0x41400000, 0x42700000, 60.0f, 0x4a5b50, 0x4a5b54) == 1) return 2;
        return bits(s) >= 0x41400000 ? g : 2;
    case 4:   /* down to third: 0.87 throttle below 98, or below the line from 98 between 31 and 98 */
        if (bits(thr) > 0x3f5eb852 && bits(s) < 0x42c40000) return 3;
        if (shift_line(thr, s, 0x3e800000, 0x41f80000, 0x42c40000, 98.0f, 0x4a5b68, 0x4a5b6c) == 1) return 3;
        return bits(s) >= 0x41f80000 ? g : 3;
    }
    return g;
}

/* the up shifts: second to third above 25 km/h (or above the line from 62 for throttles from 1/16), third
   to fourth above 40 (the line from 106) */
static int shift_up(float thr, float s, int g, int32_t from, int32_t to, float lo, uint32_t k)
{
    int c;
    if (bits(thr) < 0x3d800000 && bits(s) > from) return g + 1;
    if (bits(thr) < 0x3d800000) c = -1;
    else c = shift_line(thr, s, 0x3d800000, from, to, lo, 0x4a5b2c, k);
    if (c == 0 || bits(s) > to) return g + 1;
    return g;
}

/* toward target at twice the step's rate, unless the step is out of [0, 0.95] or the gap is below 0.005
   (then there at once); the gap as the stack has it, the product from its float */
static int settle(double k2, float k2f)
{
    return below(k2, 0) || bits(k2f) > 0x3f733333;
}

void Engine_Step(uint8_t *e, float thr, float spd, uint32_t flags, int neutral, int mode)
{
    float k = g_clock_step;
    float as = fabsf(spd);
    float r = F32(MUL(MUL(as, exe_f32(0x4a5b70)), 60.0f));   /* the wheels' turns per minute */
    int run = flags & 1, out = flags & 6, g;
    float R;
    if (neutral) {
        g = 0;
    } else {
        g = rd32(e + 8);
        if (!g) put32(e + 8, 1), g = 1;
    }
    if (mode == 2 || mode == 0) g = 1, put32(e + 8, 1);
    if (run && !out && mode != 2 && mode != 0) {
        if (g) {
            float s = F32(fabs(MUL(spd, 3.6f)));
            int t = rd32(e + 8);
            if (t == 1) {
                if (bits(thr) > 0x3b03126f) t = 2;
            } else if (t == 2) {
                if (mode != 5) t = shift_up(thr, s, t, 0x41c80000, 0x42780000, 62.0f, 0x4a5b30);
            } else if (t == 3) {
                if (mode != 5 && mode != 4) t = shift_up(thr, s, t, 0x42200000, 0x42d40000, 106.0f, 0x4a5b3c);
            }
            g = Gear_Choose(thr, spd, t, mode);
        }
        put32(e + 8, g);
        if (g == 1) R = 1050.0f;
        else R = F32(ADD(MUL(MUL(exe_f32(0x4d5cc0 + 4 * (uint32_t)g), r), exe_f32(0x4a5b74)), 850.0f));
        putf(e + 0x24, F32(MUL(MUL(MUL(r, exe_f32(0x4a5b7c)), exe_f32(0x4a5b80)), exe_f32(0x4a5b84))));
    } else {
        double k2 = MUL(k, 2.0f);
        float e1c = rdf(e + 0x1c);
        if (!run) {   /* the engine off: the rpm down to 0 */
            if (settle(k2, F32(k2))) {
                R = 0;
            } else {
                float v = -e1c;
                R = below(fabs(v), 0.005) ? 0 : F32(ADD(MUL(MUL(k, v), 2.0f), e1c));   /* 0x4a5b90 */
            }
        } else if (bits(thr) < 0x3b03126f) {   /* idling at 1050 */
            if (settle(k2, F32(k2))) {
                R = 1050.0f;
            } else {
                double vd = SUB(1050.0f, e1c);
                R = below(fabs(vd), 0.005) ? 1050.0f : F32(ADD(MUL(MUL(F32(vd), k), 2.0f), e1c));
            }
        } else {   /* revving free: 1050 + 4950 by the throttle, at most 6000 */
            if (settle(k2, F32(k2))) {
                R = F32(ADD(MUL(thr, 4950.0f), 1050.0f));
            } else {
                double wd = MUL(thr, 4950.0f);
                double ud = ADD(SUB(wd, e1c), 1050.0f);
                if (below(fabs(ud), 0.005)) R = F32(ADD(F32(wd), 1050.0f));
                else R = F32(ADD(MUL(MUL(F32(ud), k), 2.0f), e1c));
            }
            if (bits(R) > 0x45bb8000) R = 6000.0f;
        }
        /* the gauge's speed toward the road speed */
        if (settle(k2, F32(k2))) {
            putf(e + 0x24, as);
        } else {
            double vd = SUB(as, rdf(e + 0x24));
            if (below(fabs(vd), 0.005)) putf(e + 0x24, as);
            else putf(e + 0x24, F32(ADD(MUL(MUL(k, F32(vd)), 2.0f), rdf(e + 0x24))));
        }
    }
    putf(e + 0x1c, R);
    float a = F32(SUB(R, 3500.0f));   /* 0x4a5ba0 */
    putf(e + 0x10, a);
    putf(e + 0x10, F32(SUB(rdf(e + 0x14), MUL(MUL(rdf(e + 0xc), a), a))));
    putf(e + 0x10, F32(MUL(rdf(e + 0x10), rdf(e + 0x18))));
}
