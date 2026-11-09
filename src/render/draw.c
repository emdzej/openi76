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
    if (VIEW_ZNEAR(view) > v->z || !(VIEW_ZFAR(view) >= v->z)) return;   /* fld near; fcomp z: C0|C3 / fld far: C0 */
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
    float n = VIEW_ZNEAR(view), f = VIEW_ZFAR(view);
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

#ifdef OPENI76_GL
/* The port's GL backend (gl_poly.h, openi76-gl.wasm): the fillers it draws too, by the fill table's routine,
   and their vertices as its own. Not the original's. */
#include "render/gl_poly.h"
enum { SH_NONE, SH_FLAT, SH_VERTEX };   /* the shade: none, vertex 0's for the polygon, each vertex's */
static const struct {
    FillFn *fn;
    uint8_t kind, persp, tiled, full, shade, op;   /* full: u, v times the size, else the size - 1 */
} gl_fills[] = {
    {f_flat, GL_MAT_SOLID, 0, 0, 0, SH_FLAT, GL_OP_COPY},
    {f_gouraud, GL_MAT_SHADED, 0, 0, 0, SH_VERTEX, GL_OP_COPY},
    {f_shadow, GL_MAT_SOLID, 0, 0, 0, SH_NONE, GL_OP_SHADOW},
    {f_affine, GL_MAT_TEXTURE, 0, 0, 0, SH_NONE, GL_OP_COPY},
    {f_tiled, GL_MAT_TEXTURE, 0, 1, 1, SH_NONE, GL_OP_COPY},
    {f_remap, GL_MAT_TEXTURE, 0, 0, 0, SH_NONE, GL_OP_REMAP},
    {f_tbl, GL_MAT_TEXTURE, 0, 0, 0, SH_NONE, GL_OP_TBL},
    {f_tblrev, GL_MAT_TEXTURE, 0, 0, 0, SH_NONE, GL_OP_TBL_REV},
    {f_glow, GL_MAT_TEXTURE, 0, 0, 0, SH_NONE, GL_OP_GLOW},
    {f_persp, GL_MAT_TEXTURE, 1, 0, 0, SH_NONE, GL_OP_COPY},
    {f_persp_remap, GL_MAT_TEXTURE, 1, 0, 0, SH_NONE, GL_OP_REMAP},
    {f_persp_tbl, GL_MAT_TEXTURE, 1, 0, 0, SH_NONE, GL_OP_TBL},
    {f_persp_tblrev, GL_MAT_TEXTURE, 1, 0, 0, SH_NONE, GL_OP_TBL_REV},
    {f_persp_tiled, GL_MAT_TEXTURE, 1, 1, 1, SH_NONE, GL_OP_COPY},
    {f_persp_tiledkey, GL_MAT_TEXTURE, 1, 1, 1, SH_NONE, GL_OP_KEY},
    {f_persp_shade, GL_MAT_TEXTURE_SHADED, 1, 0, 0, SH_VERTEX, GL_OP_COPY},
    {f_persp_shadekey, GL_MAT_TEXTURE_SHADED, 1, 0, 0, SH_VERTEX, GL_OP_KEY},
    {f_persp_shadetiled, GL_MAT_TEXTURE_SHADED, 1, 1, 0, SH_VERTEX, GL_OP_COPY},
    {f_persp_shadetiledkey, GL_MAT_TEXTURE_SHADED, 1, 1, 0, SH_VERTEX, GL_OP_KEY},
    {f_persp_flat, GL_MAT_TEXTURE_SHADED, 1, 0, 0, SH_FLAT, GL_OP_COPY},
    {f_persp_flattiled, GL_MAT_TEXTURE_SHADED, 1, 1, 1, SH_FLAT, GL_OP_COPY},
};
static int8_t gl_fill[512];   /* the mode's entry in gl_fills, or -1 */

/* before the fill (which rewrites the vertices), under its rounding: the polygon as the fillers would take
   it, recorded; then the rectangle its pixels can fall in */
static bool gl_tap(const Surface *s, const ScreenVert *v, int n, int k, PolyArg a)
{
    GlVert g[72];
    GlMaterial m = {gl_fills[k].kind, gl_fills[k].op, (uint8_t)a.colour, NULL, gl_fills[k].persp, gl_fills[k].tiled, 0};
    float su = 0, sv = 0, row = 0;
    int shade = gl_fills[k].shade;
    if (shade == SH_FLAT)   /* the flat fillers' row, round((1 - q0) * 7936) & 0xff00, held mid-row */
        row = (float)((fpu_int(fpu_mul(fpu_sub(1.0, v[0].q), 7936.0)) & 0xff00) >> 8) + 0.5f;
    if (m.kind == GL_MAT_SOLID && shade == SH_FLAT) m.colour = g_lum[((int)row << 8) + (uint8_t)a.colour];
    if (m.kind >= GL_MAT_TEXTURE) {
        if (a.tex->w < 1 || a.tex->h < 1) return false;
        m.tex = a.tex;
        /* the row table the fill reads (the tiled affine filler's stale row); where its build faults the
           fill draws nothing */
        if (!Poly_TexRows(a.tex, gl_fills[k].fn == f_tiled, &m.rows_last)) return false;
        su = (float)(a.tex->w - !gl_fills[k].full), sv = (float)(a.tex->h - !gl_fills[k].full);
    }
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int i = 0; i < n; i++) {
        float x = v[i].x + (float)s->x0, y = v[i].y + (float)s->y0;
        /* the Gouraud fillers' shade: (1 - q) * 7936 in 8.8, the row its upper byte */
        g[i] = (GlVert){x, y, v[i].w, v[i].u * su, v[i].v * sv, shade == SH_VERTEX ? (1.0f - v[i].q) * 31.0f : row};
        if (m.persp) g[i].u /= v[i].w, g[i].v /= v[i].w;
        if (x < x0) x0 = x;
        if (x > x1) x1 = x;
        if (y < y0) y0 = y;
        if (y > y1) y1 = y;
    }
    if (!(x0 > -1e6f && x1 < 1e6f && y0 > -1e6f && y1 < 1e6f) || !gl_poly_draw(g, n, &m)) return false;
    gl_poly_cover_begin((int)x0 - 2, (int)y0 - 2, (int)x1 + 2, (int)y1 + 2, m.op >= GL_OP_REMAP);
    return true;
}

/* the pixels the fill left as they were and the backend cannot tell were written (gl_poly.h): the fill
   again into its probe, from the vertices as they were (the Gouraud fillers rewrite q), the fill's state
   kept as the first left it (the row table is this texture's already) */
static void gl_probe(const Surface *s, const ScreenVert *v, int n, int mode, PolyArg a)
{
    int stale = g_poly_stale_row;
    for (int pass = 0; pass < 2; pass++) {
        Surface p = *s;
        if (!(p.pixels = gl_poly_probe(pass))) return;
        ScreenVert w[72];
        memcpy(w, v, (size_t)n * sizeof *v);
        fill_table[mode](&p, w, n, a);
        g_poly_stale_row = stale;
        if (!gl_poly_probe_end(pass)) return;
    }
}
#endif

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
#ifdef OPENI76_GL
        gl_fill[m] = -1;
        for (size_t k = 0; k < sizeof gl_fills / sizeof *gl_fills; k++)
            if (gl_fills[k].fn == fill_table[m]) gl_fill[m] = (int8_t)k;
#endif
    }
    return true;
}

int Draw_Clip(const View *view, void *out, void *in, int n, int mode)
{
    return clip_table[mode & 0x1ff] ? clip_table[mode & 0x1ff](view, out, in, n, mode) : 0;
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
#ifdef OPENI76_GL
    ScreenVert before[72];
    bool gl = gl_fill[mode] >= 0 && gl_poly_on(view->surface->pixels);
    if (gl) {
        const Surface *sf = view->surface;
        gl_poly_view(view, sf->x0, sf->y0, sf->x1, sf->y1, VIEW_ZNEAR(view));
        gl = gl_tap(sf, out, n, gl_fill[mode], arg);
        if (gl) memcpy(before, out, (size_t)n * sizeof *out);
    }
#endif
    fill_table[mode](view->surface, out, n, arg);
#ifdef OPENI76_GL
    if (gl && gl_poly_cover_end()) gl_probe(view->surface, before, n, mode, arg);
#endif
    g_fpu_round = saved;
}
