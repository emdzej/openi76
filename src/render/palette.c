/* The palette module (palette.h). */
#include "render/palette.h"
#include "render/surface.h"
#include <math.h>
#include <string.h>

float g_pal_rgb[256][3];

void Palette_Set(const uint8_t pal[768], const uint8_t *lum)
{
    const float k = 1.0f / 255.0f;   /* 0x4a5ef8 */
    for (int i = 0; i < 256; i++)
        for (int c = 0; c < 3; c++) g_pal_rgb[i][c] = (float)((double)pal[3 * i + c] * k);
    if (lum) memcpy(g_lum, lum, 0x10000);
}

int Color_Match(float r, float g, float b)
{
    float best = 1e6f;
    int c = 0;
    for (int i = 0; i < 256; i++) {
        double dr = (double)r - g_pal_rgb[i][0], dg = (double)g - g_pal_rgb[i][1], db = (double)b - g_pal_rgb[i][2];
        double d = (dg * dg + dr * dr) + db * db;
        if (d < best) {   /* the unrounded sum against the stored float */
            c = i;
            best = (float)d;
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
    h = (float)((double)h * (1.0f / 60.0f));
    int i = (int)floor(h);
    double fr = (double)h - i;
    float p = (float)((1.0 - s) * v);
    float q = (float)((1.0 - s * fr) * v);
    float t = (float)((1.0 - (1.0 - fr) * s) * v);
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
        double dr = (double)r - g_pal_rgb[i][0], dg = (double)g - g_pal_rgb[i][1], db = (double)b - g_pal_rgb[i][2];
        double d = order == RGB_GBR ? (dg * dg + db * db) + dr * dr : (dr * dr + dg * dg) + db * db;
        if (d < best) {
            c = i;
            best = (float)d;
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
        float r = (float)(pal[3 * c] * (double)k), g = (float)(pal[3 * c + 1] * (double)k);
        float b = (float)(pal[3 * c + 2] * (double)k);
        float mx = g > r ? g : r;
        if (mx < b) mx = b;
        float mn = g < r ? g : r;
        if (mn > b) mn = b;
        float s = !(fabs((double)mx) > eps) ? 0.0f : (float)(((double)mx - mn) / mx);
        float delta = (float)((double)mx - mn), h;
        if (fabs((double)s) < eps) {
            h = -1.0f;
        } else {
            if (fabs((double)r - mx) < eps) h = (float)(((double)g - b) / delta);
            else if (fabs((double)g - mx) < eps) h = (float)(((double)b - r) / delta + 2.0f);
            else h = (float)(((double)r - g) / delta + 4.0f);
            double h60 = (double)h * 60.0f;
            h = (float)h60;
            if (h60 < 0.0f) h = (float)((double)h + 360.0f);
        }
        /* 16 steps of value up from the colour's own, to about 0.95 */
        float step = 0.35f;
        if ((double)mx + 0.35f > 0.95f) step = (float)(0.95f - (double)mx);
        step = (float)((double)step * 0.0625f);
        float v = mx;
        double sum = v;
        for (int i = 0; i < 16; i++) {
            if (i) v = (float)sum;
            hsv_rgb(h, s, v, &r, &g, &b);
            glow[i * 256 + c] = (uint8_t)nearest(r, g, b, RGB_GBR);
            sum = (double)step + v;
        }
        /* the shadow: 0.6 below the last step */
        double dv = sum - 0.6f;
        v = dv < 0.0f ? 0.0f : (float)dv;
        hsv_rgb(h, s, v, &r, &g, &b);
        shadow[c] = (uint8_t)nearest(r, g, b, RGB_RGB);
    }
    /* colours 0xe0-0xef glow as themselves at every intensity */
    for (int c = 0xe0; c < 0xf0; c++) {
        float r = (float)(pal[3 * c] * (double)k), g = (float)(pal[3 * c + 1] * (double)k);
        float b = (float)(pal[3 * c + 2] * (double)k);
        int m = nearest(r, g, b, RGB_GBR);
        for (int i = 0; i < 16; i++) glow[i * 256 + c] = (uint8_t)m;
    }
}

uint8_t g_gamma[10][256];
uint8_t g_gamma_index;

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
    if (n < 1) n = g_gamma_index;
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
