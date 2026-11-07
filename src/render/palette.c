/* The palette module (palette.h). */
#include "render/palette.h"
#include "render/surface.h"
#include "settings.h"
#include "render/fpu.h"
#include <math.h>
#include <string.h>

/* in the current rounding mode (render/fpu.h): a mission's load after the first runs rounding down */
#define ADD fpu_add
#define SUB fpu_sub
#define MUL fpu_mul
#define DIV fpu_div
#define F32 fpu_f32

float g_pal_rgb[256][3];

void Palette_Set(const uint8_t pal[768], const uint8_t *lum)
{
    const float k = 1.0f / 255.0f;   /* 0x4a5ef8 */
    for (int i = 0; i < 256; i++)
        for (int c = 0; c < 3; c++) g_pal_rgb[i][c] = F32(MUL(pal[3 * i + c], k));
    if (lum) memcpy(g_lum, lum, 0x10000);
}

int Color_Match(float r, float g, float b)
{
    float best = 1e6f;
    int c = 0;
    for (int i = 0; i < 256; i++) {
        double dr = SUB(r, g_pal_rgb[i][0]), dg = SUB(g, g_pal_rgb[i][1]), db = SUB(b, g_pal_rgb[i][2]);
        double d = ADD(ADD(MUL(dg, dg), MUL(dr, dr)), MUL(db, db));
        if (d < best) {   /* the unrounded sum against the stored float */
            c = i;
            best = F32(d);
        }
    }
    return c;
}

void Color_Get(int c, float *r, float *g, float *b)
{
    *r = g_pal_rgb[c][0];
    *g = g_pal_rgb[c][1];
    *b = g_pal_rgb[c][2];
}

void Remap_Reset(void)
{
    uint8_t *remap = g_rtab + RT_REMAP, *ident = g_rtab + 0x11000;
    for (int t = 0; t < 256; t++) {
        ident[t] = (uint8_t)t;
        memset(remap + t * 256, t, 256);
    }
    memset(g_rtab + 0x11100, 0xff, 256);
}

void Remap_Set(int t, uint8_t c) { memset(g_rtab + RT_REMAP + (t & 0xff) * 256, c, 256); }

void Remap_Key(uint8_t c)
{
    memcpy(g_rtab + RT_REMAP + c * 256, g_rtab + 0x11000, 256);
    memset(g_rtab + 0x11100, c, 256);
    memcpy(g_rtab + RT_TBL + c * 256, g_rtab + 0x11000, 256);
}

void Lum_Ramp(int from, int to, uint8_t c)
{
    int32_t v = (from & 0xff) * 0x10000, d = (to & 0xff) * 0x10000 - (from & 0xff) * 0x10000;
    int32_t step = d / 32;   /* the shift with the sign correction: towards zero */
    for (int k = 0; k < 32; k++, v += step) g_lum[k * 256 + c] = (uint8_t)((uint32_t)(v + 0x8000) >> 16);
    for (int k = 32; k < 144; k++) g_lum[k * 256 + c] = g_lum[31 * 256 + c];
    for (int k = 255; k > 143; k--) g_lum[k * 256 + c] = g_lum[c];
}

/* Hsv_ToRgb 0x43baf0: h in degrees (-1: grey), s, v. With s ~ 0 and h not -1, and for a sector past 5,
   the outputs are left as they were. */
static void hsv_rgb(float h, float s, float v, float *r, float *g, float *b)
{
    int32_t hb;
    memcpy(&hb, &h, 4);
    if (fabs((double)s) < 1e-4f) {   /* the double constant 0x4a5f00 is the float 1e-4 */
        if (hb == (int32_t)0xbf800000) *r = *g = *b = v;
        return;
    }
    if (hb >= 0x43b3fffd) h = 0;   /* by the bits: about 360 and above */
    h = F32(MUL(h, 1.0f / 60.0f));
    int i = (int)floor(h);
    double fr = SUB(h, i);
    float p = F32(MUL(SUB(1.0, s), v));
    float q = F32(MUL(SUB(1.0, MUL(s, fr)), v));
    float t = F32(MUL(SUB(1.0, MUL(SUB(1.0, fr), s)), v));
    switch (i) {
    case 0: *r = v, *g = t, *b = p; break;
    case 1: *r = q, *g = v, *b = p; break;
    case 2: *r = p, *g = v, *b = t; break;
    case 3: *r = p, *g = q, *b = v; break;
    case 4: *r = t, *g = p, *b = v; break;
    case 5: *r = v, *g = p, *b = q; break;
    default: break;
    }
}

/* the nearest-colour searches of Glow_Build, each with its own order of the three squares */
enum { RGB_GBR, RGB_RGB };
static int nearest(float r, float g, float b, int order)
{
    float best = 1e6f;
    int c = 0;
    for (int i = 0; i < 256; i++) {
        double dr = SUB(r, g_pal_rgb[i][0]), dg = SUB(g, g_pal_rgb[i][1]), db = SUB(b, g_pal_rgb[i][2]);
        double d = order == RGB_GBR ? ADD(ADD(MUL(dg, dg), MUL(db, db)), MUL(dr, dr))
                                    : ADD(ADD(MUL(dr, dr), MUL(dg, dg)), MUL(db, db));
        if (d < best) {
            c = i;
            best = F32(d);
        }
    }
    return c;
}

void Glow_Build(const uint8_t pal[768])
{
    const float k = 1.0f / 255.0f;
    const double eps = 1e-4f;
    uint8_t *glow = g_rtab + RT_GLOW, *shadow = g_rtab + 0x11200;
    for (int c = 0; c < 256; c++) {
        float r = F32(MUL(pal[3 * c], k)), g = F32(MUL(pal[3 * c + 1], k));
        float b = F32(MUL(pal[3 * c + 2], k));
        float mx = g > r ? g : r;
        if (mx < b) mx = b;
        float mn = g < r ? g : r;
        if (mn > b) mn = b;
        float s = !(fabs((double)mx) > eps) ? 0.0f : F32(DIV(SUB(mx, mn), mx));
        float delta = F32(SUB(mx, mn)), h;
        if (fabs((double)s) < eps) {
            h = -1.0f;
        } else {
            if (fabs(SUB(r, mx)) < eps) h = F32(DIV(SUB(g, b), delta));
            else if (fabs(SUB(g, mx)) < eps) h = F32(ADD(DIV(SUB(b, r), delta), 2.0f));
            else h = F32(ADD(DIV(SUB(r, g), delta), 4.0f));
            double h60 = MUL(h, 60.0f);
            h = F32(h60);
            if (h60 < 0.0f) h = F32(ADD(h, 360.0f));
        }
        /* 16 steps of value up from the colour's own, to about 0.95 */
        float step = 0.35f;
        if (ADD(mx, 0.35f) > 0.95f) step = F32(SUB(0.95f, mx));
        step = F32(MUL(step, 0.0625f));
        float v = mx;
        double sum = v;
        for (int i = 0; i < 16; i++) {
            if (i) v = F32(sum);
            hsv_rgb(h, s, v, &r, &g, &b);
            glow[i * 256 + c] = (uint8_t)nearest(r, g, b, RGB_GBR);
            sum = ADD(step, v);
        }
        /* the shadow: 0.6 below the last step */
        double dv = SUB(sum, 0.6f);
        v = dv < 0.0f ? 0.0f : F32(dv);
        hsv_rgb(h, s, v, &r, &g, &b);
        shadow[c] = (uint8_t)nearest(r, g, b, RGB_RGB);
    }
    /* colours 0xe0-0xef glow as themselves at every intensity */
    for (int c = 0xe0; c < 0xf0; c++) {
        float r = F32(MUL(pal[3 * c], k)), g = F32(MUL(pal[3 * c + 1], k));
        float b = F32(MUL(pal[3 * c + 2], k));
        int m = nearest(r, g, b, RGB_GBR);
        for (int i = 0; i < 16; i++) glow[i * 256 + c] = (uint8_t)m;
    }
}

uint8_t g_gamma[10][256];

void Gamma_Build(void)
{
    static const double gamma[10] = {0.8, 0.9, 1.0, 1.2, 1.4, 1.6, 1.8, 2.0, 2.2, 2.5};
    for (int k = 0; k < 10; k++) {
        double e = 1.0 / gamma[k];
        for (int i = 0; i < 256; i++)
            g_gamma[k][i] = (uint8_t)(int32_t)(int64_t)trunc(pow(i * (1.0 / 255.0), e) * 255.0);   /* 0x4a5fa0/a8 */
    }
}

static const uint8_t *gamma_row(int n)
{
    if (n < 1) n = g_detail.b[DET_GAMMA];
    if (n < 1 || n > 10) n = 3;
    return g_gamma[n - 1];
}

void Gamma_Apply(uint8_t *pal, int first, int end, int n)
{
    const uint8_t *g = gamma_row(n);
    for (int i = first; i < end; i++)
        for (int c = 0; c < 3; c++) pal[3 * i + c] = g[pal[3 * i + c]];
}

void Display_SetPalette(uint8_t shown[768], int first, int count, const uint8_t *pal, const uint8_t *lum)
{
    memcpy(shown + 3 * first, pal + 3 * first, 3 * (size_t)count);
    shown[0] = shown[1] = shown[2] = 0;
    Gamma_Apply(shown, first, count, 0);   /* the original passes the count as the end */
    Palette_Set(shown, lum);
}
