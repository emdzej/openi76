/* The sky (sky.h). In the current rounding mode (render/fpu.h). */
#include "render/sky.h"
#include "exe.h"
#include "render/draw.h"
#include "render/fpu.h"
#include "render/light.h"
#include "render/sprite.h"
#include "render/terrain_draw.h"
#include "settings.h"
#include "tex.h"
#include "world.h"
#include <math.h>
#include <stdbool.h>
#include <string.h>

#define ADD fpu_add
#define SUB fpu_sub
#define MUL fpu_mul
#define DIV fpu_div
#define F32 fpu_f32

const Texture *g_sky_tex;
int g_sky_sun;
Surface g_sky_sun_surf;
Frame g_sky_sun_frame;
double g_sky_scroll[2];
static int ready;   /* 0x4a8868 */
static View saved;  /* 0x4eda20 */

static bool below(double a, double b) { return a < b || isnan(a) || isnan(b); }      /* C0 */
static bool below_eq(double a, double b) { return a <= b || isnan(a) || isnan(b); }  /* C0 | C3 */
static uint32_t bits(float f) { uint32_t b; memcpy(&b, &f, 4); return b; }
/* fadd 1.5 * 2^52 (0x4db2d8) and the low dword */
static int32_t magic(double d) { return isnan(d) ? 0 : (int32_t)(uint32_t)(uint64_t)(int64_t)fpu_round(d); }

void Sky_Init(void)
{
    if (ready) return;
    g_sky_scroll[0] = g_sky_scroll[1] = 0;
    g_sky_sun = 0;
    if (!g_detail.b[DET_SKY]) return;
    /* the messages ("Sky Texture not found", "Moon or sun texture not found") are a no-op, 0x407470 */
    if (!(g_sky_tex = Tex_Pixels(g_world.sky))) return;
    if (!g_sun_map) return;
    g_sky_sun = 1;
    const Texture *m = (const Texture *)g_sun_map;
    g_sky_sun_surf = Surface_Make(m->w, m->h, (uint8_t *)m->pixels);
    Frame_AxisAngle(&g_sky_sun_frame, Light_Elevation(), exe_f32(0x4d6848), exe_f32(0x4d684c), exe_f32(0x4d6850));
    ready = 1;
}

void Sky_Free(void)
{
    if (g_sky_tex) {
        Tex_PixelsRelease(g_world.sky);
        g_sky_tex = NULL;
    }
    g_sky_sun = 0;
    ready = 0;
}

int Sky_ClipLine(float (*out)[6], const float (*in)[6], float a, float b, float c, float q, int n)
{
    int m = 0;
    if (n <= 0) return 0;
    const float *p = in[n - 1];
    float fp = F32(ADD(ADD(MUL(p[1], b), MUL(p[0], a)), c));
    for (int i = 0; i < n; i++) {
        const float *v = in[i];
        double f = ADD(ADD(MUL(v[1], b), MUL(v[0], a)), c);
        bool in_v = !below_eq(f, 0.0), in_p = (int32_t)bits(fp) > 0;
        if (in_v && in_p) {
            memcpy(out[m++], v, 24);
        } else if (in_v || in_p) {
            float *o = out[m];
            if (in_v) {
                memcpy(out[m + 1], v, 24);
                m += 2;
            } else {
                m += 1;
            }
            double dy = SUB(v[1], p[1]), dx = SUB(v[0], p[0]);
            double t = DIV(fp, ADD(MUL(b, dy), MUL(a, dx)));
            o[1] = F32(SUB(p[1], MUL(t, dy)));
            o[0] = F32(SUB(p[0], MUL(t, dx)));
            o[2] = 0;
            o[5] = q;
        }
        p = v;
        fp = F32(f);
    }
    return m;
}

/* the screen's rectangle as four screen-space vertices (x, y, w = 0, u, v, q) */
static void rect(float (*v)[6], const View *w, float q)
{
    memset(v, 0, 4 * sizeof *v);   /* the original leaves u, v as its stack had them */
    v[0][0] = w->xmin, v[0][1] = w->ymin;
    v[1][0] = w->xmax, v[1][1] = w->ymin;
    v[2][0] = w->xmax, v[2][1] = w->ymax;
    v[3][0] = w->xmin, v[3][1] = w->ymax;
    for (int i = 0; i < 4; i++) v[i][5] = q;
}

/* the horizontal plane at height h of a view, as a screen line (a, b, c): a * x + b * y + c > 0 above it;
   c unrounded too (cu, if given) */
static void line(const View *w, float h, float *a, float *b, float *c, double *cu)
{
    float p[4] = {0, 1, 0, F32(-(double)h)}, t[4];
    Plane_Transform(t, p, &w->cam);
    double da = DIV(t[0], w->sx), db = DIV(t[1], w->sy);
    double e = SUB(SUB(t[2], MUL(DIV(w->cy, w->sy), t[1])), MUL(DIV(w->cx, w->sx), t[0]));
    double cc = ADD(e, DIV(t[3], VIEW_ZFAR(w)));
    *c = F32(cc);
    *a = F32(da), *b = F32(db);
    if (cu) *cu = cc;
}

/* Sky_Flat 0x407780: the screen above the plane at height h */
static int sky_flat(float (*out)[6], const View *w, float h)
{
    float in[4][6], a, b, c;
    rect(in, w, 1.0f);
    line(w, h, &a, &b, &c, NULL);
    return Sky_ClipLine(out, (const float (*)[6])in, a, b, c, 1.0f, 4);
}

/* Sky_Ground 0x4080d0: the haze between the horizon of the terrain's lowest point (in the view as it came,
   0x4eda20) and the plane at height h (in the sky's view), its q from 1 at the low horizon's farthest
   vertex towards 0 at the plane */
static int sky_ground(float (*out)[6], const View *w, float h)
{
    float in[4][6], mid[16][6], a, b, c;
    rect(in, w, -1.0f);
    line(&saved, g_terr_low, &a, &b, &c, NULL);   /* Terrain_Low 0x48e810 */
    int n = Sky_ClipLine(mid, (const float (*)[6])in, a, b, c, 1.0f, 4);
    if (!n) return 0;
    int far = 0;
    float best = F32(ADD(MUL(b, mid[0][1]), MUL(a, mid[0][0])));
    for (int k = 1; k < n; k++) {
        double f = ADD(MUL(mid[k][0], a), MUL(mid[k][1], b));
        float g = F32(f);
        if (below(f, best)) far = k, best = g;
    }
    float fx = mid[far][0], fy = mid[far][1];
    double cu;
    line(w, h, &a, &b, &c, &cu);
    /* -a, -b from the floats; -c from c unrounded (fchs before its fstp) */
    int m = Sky_ClipLine(out, (const float (*)[6])mid, -a, -b, F32(-cu), 0, n);
    if (!m) return 0;
    float k = F32(DIV(1.0, ADD(ADD(MUL(b, fy), MUL(a, fx)), c)));
    for (int i = 0; i < m; i++) {
        double q = MUL(ADD(ADD(MUL(out[i][0], a), MUL(out[i][1], b)), c), k);
        float f = F32(q);
        if (!below_eq(q, 1.0)) f = 1.0f;
        if (bits(f) > 0x80000000u) f = 0;
        out[i][5] = f;
    }
    return m;
}

static float sign_of(float f)   /* 0, 1 or -1 by the bits */
{
    int32_t b = (int32_t)bits(f);
    return (b & 0x7fffffff) == 0 ? 0.0f : b > 0 ? 1.0f : -1.0f;
}

void Sky_Draw(View *view, int colour, int mode)
{
    if (!g_sky_tex) mode = 1;
    saved = *view;
    Frame cam;
    Frame_Invert(&cam, &saved.cam);
    /* fpatan: the C library's, to nearest (render.md, "The FPU's rounding") */
    View_Create(view, saved.surface, F32(MUL(atan2(saved.tan_half, 1.0), 2.0f)), saved.aspect, 600.0f, saved.zoom);
    view->sx = F32(MUL(fabs((double)view->sx), sign_of(saved.sx)));
    view->sy = F32(MUL(fabs((double)view->sy), sign_of(saved.sy)));
    View_SetCamera(view, &cam);
    float buf[16][6];
    PolyArg arg = {.colour = (uint32_t)colour};
    int n;
    if (mode == 0) {
        if ((n = sky_flat(buf, view, 0))) DrawPoly(view, buf, n, arg, 8);
    } else if (mode == 1) {
        float h = F32(ADD(view->eye[1], 50.0f));   /* 0x4a5134 */
        if ((n = sky_flat(buf, view, h))) {
            for (int i = 0; i < n; i++) buf[i][5] = 0;
            DrawPoly(view, buf, n, arg, 8);
        }
        if ((n = sky_ground(buf, view, F32(ADD(h, 30.0f))))) DrawPoly(view, buf, n, arg, 9);   /* 0x4a5160 */
    } else {
        float h = F32(ADD(view->eye[1], 50.0f));
        /* the clouds: a square of 3000 above the eye, its corner on the 1000 grid (0x4a87f0, its texture
           coordinates and q at 0x4a8838; the frame 0x4db298) */
        Frame f;
        memcpy(&f, exe_data(0x4db298, sizeof f), sizeof f);
        f.pos[0] = MUL(floor(MUL(view->eye[0], 0.001)), 1000.0);   /* 0x4a5138, 0x4a5140 */
        f.pos[1] = h;
        f.pos[2] = MUL(floor(MUL(view->eye[2], 0.001)), 1000.0);
        Frame m;
        Frame_Mul(&m, &f, &view->cam);
        for (int i = 0; i < 4; i++) {
            float p[3];
            memcpy(p, exe_data(0x4a87f0 + 12u * (uint32_t)i, 12), 12);
            Frame_Points((float (*)[3])buf[i], (const float (*)[3])p, 1, &m);
            buf[i][3] = F32(ADD(exe_f32(0x4a8838 + 12u * (uint32_t)i), g_sky_scroll[0]));
            buf[i][4] = F32(ADD(exe_f32(0x4a883c + 12u * (uint32_t)i), g_sky_scroll[1]));
            buf[i][5] = exe_f32(0x4a8840 + 12u * (uint32_t)i);
        }
        DrawPoly(view, buf, 4, (PolyArg){.tex = g_sky_tex}, 0x16);
        /* the drift, a step a frame (_CIfmod, exact) */
        g_sky_scroll[0] = fmod(SUB(g_sky_scroll[0], 1.0 / 1024), 1.0);   /* 0x4a5148, 0x4a5150 */
        g_sky_scroll[1] = fmod(ADD(g_sky_scroll[1], 1.0 / 1024), 1.0);
        if ((n = sky_ground(buf, view, F32(ADD(h, 80.0f))))) DrawPoly(view, buf, n, arg, 9);   /* 0x4a5158 */
        if (g_sky_sun) {
            /* the sun 300 out along its frame's y in camera space (the frame at the eye; every coordinate
               gets the position's x, which is about 0 there), a 40 x 40 square about it projected */
            memcpy(g_sky_sun_frame.pos, view->eye, sizeof view->eye);
            Frame_Mul(&m, &g_sky_sun_frame, &view->cam);
            float a = F32(ADD(MUL(m.m[3], 300.0f), m.pos[0]));   /* 0x4a515c */
            float b = F32(ADD(MUL(m.m[4], 300.0f), m.pos[0]));
            double cz = ADD(MUL(m.m[5], 300.0f), m.pos[0]);
            float c = F32(cz);
            if (below(cz, VIEW_ZNEAR(view))) goto out;
            float w = F32(DIV(1.0, c));
            const float *o = (const float *)exe_data(0x4a8820, 24);
            int32_t x0 = magic(ADD(MUL(MUL(ADD(o[0], a), view->sx), w), view->cx));
            int32_t y0 = magic(ADD(MUL(MUL(ADD(o[1], b), view->sy), w), view->cy));
            int32_t x1 = magic(ADD(MUL(MUL(ADD(o[3], a), view->sx), w), view->cx));
            int32_t y1 = magic(ADD(MUL(MUL(ADD(b, o[4]), view->sy), w), view->cy));
            Sprite_Draw(view->surface, &g_sky_sun_surf, x0, y0, x1, y1, 3, colour + 0xc);
        }
    }
out:
    *view = saved;
}
