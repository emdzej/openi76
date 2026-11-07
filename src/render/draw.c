/* DrawPoly and the routines it dispatches to. The two mode tables are read from the exe and each entry
   mapped to the port's routine for the original's address. */
#include "render/draw.h"
#include "render/clip.h"
#include "render/fpu.h"
#include "render/line.h"
#include "render/poly.h"
#include "exe.h"
#include <stdio.h>
#include <string.h>

#define ADD fpu_add
#define SUB fpu_sub
#define MUL fpu_mul
#define DIV fpu_div
#define F32 fpu_f32

int g_screen_h = 480;

/* DrawPoint 0x43d390 */
void DrawPoint(const View *view, const CamVert *v, uint8_t colour)
{
    if (view->znear > v->z || !(view->zfar >= v->z)) return;   /* fld near; fcomp z: C0|C3 / fld far: C0 */
    double w = DIV(1.0, v->z);
    int x = fpu_int(ADD(MUL(MUL(view->sx, v->x), w), view->cx));
    int y = fpu_int(ADD(MUL(w, MUL(v->y, view->sy)), view->cy));
    const Surface *s = view->surface;
    if (g_screen_h <= 480) return;
    for (int i = 0; i < 4; i++) {   /* the four pixels of the dot, each checked against the viewport */
        int dy = i / 2, dx = i % 2;
        if ((y + dy - s->y0 | s->y1 - y - dy | x + dx - s->x0 | s->x1 - dx - x) < 0) continue;
        s->pixels[(y + dy + s->y0) * s->pitch + s->x0 + x + dx] = colour;
    }
}

/* the sign bit of a float: the line's tests on z - near and far - z */
static int sign_of(float f)
{
    uint32_t b;
    memcpy(&b, &f, 4);
    return (int)(b >> 31);
}

/* one plane of the line's clip: the signs of sa, sb (the ends' distances, as floats) give the code, 1 b beyond,
   2 a beyond, 3 both (nothing to draw). The end beyond moves to the plane, x and y by t = num / (z_b - z_a). */
static bool line_plane(CamVert *a, CamVert *b, float plane, float sa, float sb, double num)
{
    int code = sign_of(sb) | sign_of(sa) << 1;
    if (code == 3) return false;
    if (code) {
        double t = DIV(num, SUB(b->z, a->z));
        CamVert *m = code & 2 ? a : b;
        float x = F32(ADD(MUL(SUB(b->x, a->x), t), a->x)), y = F32(ADD(MUL(SUB(b->y, a->y), t), a->y));
        m->z = plane, m->x = x, m->y = y;
    }
    return true;
}

/* DrawLine 0x43d4c0 */
void DrawLine(const View *view, CamVert *a, CamVert *b, uint8_t colour)
{
    float n = view->znear, f = view->zfar;
    /* near: the signs of z - near; t's numerator near - z_a unrounded */
    if (!line_plane(a, b, n, F32(SUB(a->z, n)), F32(SUB(b->z, n)), SUB(n, a->z))) return;
    /* far: the signs of far - z; t's numerator is the stored float far - z_a */
    float fa = F32(SUB(f, a->z));
    if (!line_plane(a, b, f, fa, F32(SUB(f, b->z)), fa)) return;
    double wa = DIV(1.0, a->z);
    int xa = fpu_int(ADD(MUL(MUL(view->sx, a->x), wa), view->cx));
    int ya = fpu_int(ADD(MUL(wa, MUL(a->y, view->sy)), view->cy));
    double wb = DIV(1.0, b->z);
    int xb = fpu_int(ADD(MUL(MUL(view->sx, b->x), wb), view->cx));
    int yb = fpu_int(ADD(MUL(wb, MUL(b->y, view->sy)), view->cy));
    Line_Draw(view->surface, xa, ya, xb, yb, colour, 0);
    if (g_screen_h <= 480) return;
    /* the second line: below when dx > dy, else to the right (signed differences, not lengths) */
    if (xb - xa > yb - ya) Line_Draw(view->surface, xa, ya + 1, xb, yb + 1, colour, 0);
    else Line_Draw(view->surface, xa + 1, ya, xb + 1, yb, colour, 0);
}

/* Poly_Outline 0x43e940: from the last vertex round, each edge's ends rounded (in DrawPoly's rounding) */
void Poly_Outline(const Surface *s, const ScreenVert *v, int n, uint8_t colour)
{
    if (n < 1) return;
    int px = fpu_int((double)v[n - 1].x), py = fpu_int((double)v[n - 1].y);
    for (int i = 0; i < n; i++) {
        int x = fpu_int((double)v[i].x), y = fpu_int((double)v[i].y);
        Line_Draw(s, px, py, x, y, colour, 0);
        px = x, py = y;
    }
}

/* the clip table's routines */
typedef int ClipFn(const View *view, ScreenVert *out, void *in, int n, int mode);

static int c_proj(const View *w, ScreenVert *o, void *in, int n, int m) { return Proj_Untextured(w, o, in, n, m); }
static int c_proj_tex(const View *w, ScreenVert *o, void *in, int n, int m) { return Proj_Textured(w, o, in, n, m); }
static int c_screen(const View *w, ScreenVert *o, void *in, int n, int m) { return Clip_Screen(w, o, in, n, m); }
static int c_screen_tex(const View *w, ScreenVert *o, void *in, int n, int m) { return Clip_ScreenTex(w, o, in, n, m); }
static int c_camera(const View *w, ScreenVert *o, void *in, int n, int m) { return Clip_Camera(w, o, in, n, m); }
static int c_camera_tex(const View *w, ScreenVert *o, void *in, int n, int m) { return Clip_CameraTex(w, o, in, n, m); }
/* Clip_Copy 0x441440: the input as it is */
static int c_copy(const View *w, ScreenVert *o, void *in, int n, int m)
{
    memcpy(o, in, (size_t)n * sizeof *o);
    return n;
}
/* Clip_Error 0x441470: "Error in clipping function" to a debug print the release build left empty */
static int c_error(const View *w, ScreenVert *o, void *in, int n, int m) { return 0; }

/* the fill table's */
typedef void FillFn(const Surface *s, ScreenVert *v, int n, PolyArg arg);

#define FILL_COLOUR(name, fn) \
    static void name(const Surface *s, ScreenVert *v, int n, PolyArg a) { fn(s, v, n, (uint8_t)a.colour); }
#define FILL_TEX(name, fn) \
    static void name(const Surface *s, ScreenVert *v, int n, PolyArg a) { fn(s, v, n, a.tex); }
FILL_COLOUR(f_flat, Poly_FillFlat)
FILL_COLOUR(f_gouraud, Poly_FillGouraud)
FILL_COLOUR(f_shadow, Poly_FillShadow)
FILL_COLOUR(f_outline, Poly_Outline)
FILL_TEX(f_affine, Poly_FillTexAffine)
FILL_TEX(f_remap, Poly_FillTexRemap)
FILL_TEX(f_tbl, Poly_FillTexTbl)
FILL_TEX(f_tblrev, Poly_FillTexTblRev)
FILL_TEX(f_glow, Poly_FillTexGlow)
FILL_TEX(f_tiled, Poly_FillTexTiled)
FILL_TEX(f_persp, Poly_FillTexPersp)
FILL_TEX(f_persp_remap, Poly_FillTexPerspRemap)
FILL_TEX(f_persp_tbl, Poly_FillTexPerspTbl)
FILL_TEX(f_persp_tblrev, Poly_FillTexPerspTblRev)
FILL_TEX(f_persp_tiled, Poly_FillTexPerspTiled)
FILL_TEX(f_persp_tiledkey, Poly_FillTexPerspTiledKey)
FILL_TEX(f_persp_shade, Poly_FillTexPerspShade)
FILL_TEX(f_persp_shadekey, Poly_FillTexPerspShadeKey)
FILL_TEX(f_persp_shadetiled, Poly_FillTexPerspShadeTiled)
FILL_TEX(f_persp_shadetiledkey, Poly_FillTexPerspShadeTiledKey)
FILL_TEX(f_persp_flat, Poly_FillTexPerspFlat)
FILL_TEX(f_persp_flattiled, Poly_FillTexPerspFlatTiled)
/* 0x447e80, 0x4449a0: a bare ret */
static void f_none(const Surface *s, ScreenVert *v, int n, PolyArg a) {}

static const struct { uint32_t addr; ClipFn *fn; } clip_fns[] = {
    {0x4412e0, c_proj}, {0x441340, c_proj_tex}, {0x440350, c_screen}, {0x43fbf0, c_screen_tex},
    {0x4409c0, c_camera}, {0x440d90, c_camera_tex}, {0x441440, c_copy}, {0x441470, c_error},
};
static const struct { uint32_t addr; FillFn *fn; } fill_fns[] = {
    {0x43ea00, f_flat}, {0x43ecd0, f_gouraud}, {0x442a70, f_shadow}, {0x43d370, f_outline},
    {0x44a570, f_affine}, {0x449640, f_affine}, {0x446180, f_affine}, {0x4470b0, f_remap},
    {0x442d90, f_tbl}, {0x443320, f_tblrev}, {0x4424e0, f_glow}, {0x448870, f_tiled}, {0x4453a0, f_tiled},
    {0x44aaf0, f_persp}, {0x447640, f_persp_remap}, {0x443910, f_persp_tbl}, {0x444150, f_persp_tblrev},
    {0x448e00, f_persp_tiled}, {0x445930, f_persp_tiledkey}, {0x449bc0, f_persp_shade},
    {0x446700, f_persp_shadekey}, {0x447e90, f_persp_shadetiled}, {0x4449b0, f_persp_shadetiledkey},
    {0x441480, f_persp_flat}, {0x441c90, f_persp_flattiled}, {0x447e80, f_none}, {0x4449a0, f_none},
};

static ClipFn *clip_table[512];
static FillFn *fill_table[512];

bool Draw_Init(char *err, size_t cap)
{
    if (!exe_loaded()) return snprintf(err, cap, "the exe is not loaded"), false;
    for (int m = 0; m < 512; m++) {
        uint32_t c = exe_u32(0x4d7488 + 4u * (uint32_t)m), f = exe_u32(0x4d6c88 + 4u * (uint32_t)m);
        clip_table[m] = NULL, fill_table[m] = NULL;
        for (size_t k = 0; k < sizeof clip_fns / sizeof *clip_fns; k++)
            if (clip_fns[k].addr == c) clip_table[m] = clip_fns[k].fn;
        for (size_t k = 0; k < sizeof fill_fns / sizeof *fill_fns; k++)
            if (fill_fns[k].addr == f) fill_table[m] = fill_fns[k].fn;
        if (!clip_table[m] || !fill_table[m])
            return snprintf(err, cap, "mode %#x: no port of clip %#x / fill %#x", m, c, f), false;
    }
    return true;
}

/* DrawPoly 0x43d730 */
void DrawPoly(const View *view, void *verts, int n, PolyArg arg, int mode)
{
    mode &= 0x1ff;
    if (n == 1) {
        DrawPoint(view, verts, (uint8_t)arg.colour);
        return;
    }
    if (n == 2) {   /* a copy of the ends' x, y, z: the line clips them in place */
        const CamVert *v = verts;
        CamVert a = {v[0].x, v[0].y, v[0].z, 0, 0, 0}, b = {v[1].x, v[1].y, v[1].z, 0, 0, 0};
        DrawLine(view, &a, &b, (uint8_t)arg.colour);
        return;
    }
    ScreenVert out[72];   /* the original's has room for 17; the clippers' buffers hold 72 */
    if (!clip_table[mode]) return;
    n = clip_table[mode](view, out, verts, n, mode);
    if (!n) return;
    /* the fill runs with the FPU rounding up (the control word's RC = 2); a fault in a filler is caught */
    int saved = g_fpu_round;
    g_fpu_round = FPU_UP;
    fill_table[mode](view->surface, out, n, arg);
    g_fpu_round = saved;
}
