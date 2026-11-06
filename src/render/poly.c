/* Polygon fillers. The originals are hand-written x87 assembly; the port keeps their arithmetic (the
   rounding through 1.5 * 2^52, the floats stored at the same points) so the pixels match, which
   tools/emu/diff_poly.py checks against the original machine code. */
#include "render/poly.h"
#include "render/fpu.h"
#include <string.h>

uint8_t g_rtab[RT_SIZE];
int g_fpu_round = FPU_NEAREST;

#define round_magic fpu_int
#define ADD fpu_add
#define SUB fpu_sub
#define MUL fpu_mul
#define DIV fpu_div
#define F32 fpu_f32

/* One edge's start: x at the start vertex's rounded y, and the step per row (both stored as floats). */
static void edge_setup(const ScreenVert *a, const ScreenVert *b, float *x, float *step)
{
    double inv = DIV(1.0, SUB(b->y, a->y));
    int32_t ya = round_magic((double)a->y);
    double slope = MUL(SUB(b->x, a->x), inv);
    *step = F32(slope);
    *x = F32(ADD(MUL(SUB((double)ya, a->y), slope), a->x));
}

/* Poly_FillFlat 0x43ea00 */
void Poly_FillFlat(const Surface *s, const ScreenVert *v, int n, uint8_t colour)
{
    int next[64], prev[64], yi[64];
    if (n < 3 || n > 64) return;
    int top = 0, ymin = 10000;
    for (int i = 0; i < n; i++) {
        next[i] = i + 1 < n ? i + 1 : 0;
        prev[i] = i ? i - 1 : n - 1;
        yi[i] = round_magic((double)v[i].y);
        if (yi[i] <= ymin) ymin = yi[i], top = i;   /* ties: the later vertex */
    }
    int shade = round_magic(MUL(SUB(1.0, v[0].q), 7936.0));
    uint8_t c = g_lum[(shade & 0xff00) + colour];
    uint8_t *row = s->pixels + (ymin + s->y0) * s->pitch + s->x0;
    int l = top, r = top, y = ymin;
    float lx = 0, ls = 0, rx = 0, rs = 0;
    for (;;) {
        if (yi[l] <= y) {   /* the left edge (along next) ends: the next one that goes below this row */
            int a;
            do {
                a = l;
                l = next[a];
                if (l == top) return;
            } while (yi[l] <= y);
            edge_setup(&v[a], &v[l], &lx, &ls);
        }
        if (yi[r] <= y) {   /* the right edge (along prev); no check for the top here, as the original */
            int a;
            do {
                a = r;
                r = prev[a];
            } while (yi[r] <= y);
            edge_setup(&v[a], &v[r], &rx, &rs);
        }
        int x1 = round_magic((double)lx), x2 = round_magic((double)rx);
        if (x1 != x2) {
            if (x2 < x1) {
                int t = x1;
                x1 = x2, x2 = t;
            }
            memset(row + x1, c, (size_t)(x2 - x1));
        }
        row += s->pitch;
        y++;
        lx = F32(ADD(lx, ls));
        rx = F32(ADD(rx, rs));
    }
}

/* add, then adc 0: the 16.16 value kept rotated (integer in the low half), the fraction's carry going round
   into the integer. A negative step's integer half carries into the fraction; that is the original's too. */
static uint32_t add_endaround(uint32_t v, uint32_t s)
{
    uint32_t r = v + s;
    return r + (r < s);
}

static uint32_t rol16(int32_t x) { return (uint32_t)x << 16 | (uint32_t)x >> 16; }

/* An edge of the Gouraud filler: x and shade at the start vertex's rounded y, both steps rounded to floats
   first and the start values computed from the rounded steps. */
static void edge_setup_shade(const ScreenVert *a, const ScreenVert *b, float *x, float *dx, float *sh, float *ds)
{
    double inv = DIV(1.0, SUB(b->y, a->y));
    double t = SUB((double)round_magic((double)a->y), a->y);
    *dx = F32(MUL(SUB(b->x, a->x), inv));
    *ds = F32(MUL(SUB(b->q, a->q), inv));
    *x = F32(ADD(MUL(*dx, t), a->x));
    *sh = F32(ADD(MUL(t, *ds), a->q));
}

/* Poly_FillGouraud 0x43ecd0 */
void Poly_FillGouraud(const Surface *s, ScreenVert *v, int n, uint8_t colour)
{
    int next[64], prev[64];
    if (n < 1 || n > 64) return;
    int top = 0;
    double ymin = 100000.0;
    for (int i = 0; i < n; i++) {
        next[i] = i + 1;
        prev[i] = i - 1;
        v[i].q = F32(MUL(MUL(SUB(1.0, v[i].q), 7936.0), 256.0));
        if ((double)v[i].y <= ymin) top = i, ymin = (double)v[i].y;   /* ties: the later vertex */
    }
    prev[0] = n - 1;
    next[n - 1] = 0;
    int row_y = round_magic(ymin);
    uint8_t *row = s->pixels + (s->y0 + row_y) * s->pitch + s->x0;
    int l = top, r = top, stop = top;
    float lx = 0, ldx = 0, ls = 0, lds = 0, rx = 0, rdx = 0, rs = 0, rds = 0;
    for (;;) {
        if (round_magic((double)v[l].y) <= row_y) {   /* the left edge, along next */
            int a;
            do {
                a = l;
                l = next[a];
                if (l == stop) return;
            } while (round_magic((double)v[l].y) <= row_y);
            edge_setup_shade(&v[a], &v[l], &lx, &ldx, &ls, &lds);
        }
        if (round_magic((double)v[r].y) <= row_y) {   /* the right edge, along prev; the left walk stops at its start */
            int a, b = r;
            do {
                a = b;
                stop = a;
                b = prev[a];
            } while (round_magic((double)v[b].y) <= row_y);
            edge_setup_shade(&v[a], &v[b], &rx, &rdx, &rs, &rds);
            r = b;
        }
        int xl = round_magic((double)lx), xr = round_magic((double)rx);
        if (xr != xl) {
            double q = DIV(SUB(rs, ls), SUB(rx, lx));
            float qf = F32(q);
            double start;
            int x1, x2;
            if (xl < xr)
                start = ADD(MUL(SUB((double)xl, lx), qf), ls), x1 = xl, x2 = xr;
            else
                start = ADD(MUL(SUB((double)xr, rx), qf), rs), x1 = xr, x2 = xl;
            uint32_t sv = rol16(round_magic(start)), st = rol16(round_magic(q));
            uint8_t *p = row + x1;
            for (int k = x2 - x1; k > 0; k--) {
                *p++ = g_lum[(sv & 0xff) << 8 | colour];
                sv = add_endaround(sv, st);
            }
        }
        rx = F32(ADD(rx, rdx));
        rs = F32(ADD(rds, rs));
        lx = F32(ADD(lx, ldx));
        ls = F32(ADD(lds, ls));
        row += s->pitch;
        row_y++;
    }
}

/* The textured fillers' row table (0x5ad328): a pointer to each texture row, rebuilt only when the texture
   record differs from the last one (0x5ad310), by address. Four entries before row 0 repeat row 0 and four
   after the last row repeat it, so a coordinate rounded just outside the texture reads its edge. */
#define TEX_ROWS_MAX 2048
static const uint8_t *tex_row_buf[4 + TEX_ROWS_MAX + 8];
static const uint8_t *const *const tex_rows = tex_row_buf + 4;
static const Texture *tex_rows_of;

static void tex_rows_build(const Texture *t)
{
    if (t == tex_rows_of) return;
    tex_rows_of = t;
    const uint8_t **rows = (const uint8_t **)tex_rows;
    for (int i = -4; i < 0; i++) rows[i] = t->pixels;
    /* three rows per pass while the count left (h + 3, less three a pass) stays >= 0: the last pass writes
       past the last row, which the four copies after it then mostly cover */
    int i = 0;
    for (int c = t->h + 3; c >= 0; c -= 3, i += 3)
        for (int k = 0; k < 3; k++) rows[i + k] = t->pixels + (size_t)(i + k) * (size_t)t->w;
    for (int k = 0; k < 4; k++) rows[t->h + k] = rows[t->h - 1];
}

/* rows[v][u], as the original reads it: u past a row's end reads into the next row, just before row 0 into
   the record's header. Where the original would read outside the record (a coordinate extrapolated far off the
   texture on a near-horizontal edge or a sliver of a polygon) it reads whatever memory follows; the port
   reads the nearest byte of the record instead: the one place it does not match the original, by necessity. */
static inline const uint8_t *tex_texel(const Texture *t, int32_t v, int32_t u)
{
    const uint8_t *lo = (const uint8_t *)t, *hi = t->pixels + (size_t)t->w * (size_t)t->h - 1;
    const uint8_t *p = tex_rows[v < -4 ? -4 : v > t->h + 3 ? t->h + 3 : v] + u;
    return p < lo ? lo : p > hi ? hi : p;
}

#define MAGIC 6755399441055744.0   /* 1.5 * 2^52: added in the current rounding mode, its low dword is the integer */
static int32_t magic_lo(double s) { return (int32_t)(int64_t)(s - MAGIC); }

/* An edge of the affine textured filler: x, u, v at the start vertex's rounded y and their steps per row.
   The steps stay doubles for the start values and are stored as floats. */
typedef struct { float x, u, v, dx, du, dv; } TexEdge;

static void edge_setup_tex(const ScreenVert *a, const ScreenVert *b, TexEdge *e)
{
    int32_t ya = round_magic((double)a->y);
    double inv = DIV(1.0, SUB(b->y, a->y));
    double dv = MUL(SUB(b->v, a->v), inv), du = MUL(SUB(b->u, a->u), inv), dx = MUL(SUB(b->x, a->x), inv);
    double t = SUB((double)ya, a->y);
    e->du = F32(du), e->dv = F32(dv), e->dx = F32(dx);
    e->v = F32(ADD(MUL(dv, t), a->v));
    e->u = F32(ADD(MUL(du, t), a->u));
    e->x = F32(ADD(MUL(t, dx), a->x));
}

/* What the textured fillers write for a texel t over the destination pixel d. The variants are copies of one
   routine with a different pixel line (the index is t << 8 | d in a byte register pair). */
enum { PIX_COPY, PIX_REMAP, PIX_TBL, PIX_TBL_REV, PIX_GLOW };

static inline void tex_put(uint8_t *p, int op, uint8_t t)
{
    switch (op) {
    case PIX_COPY: *p = t; break;
    case PIX_REMAP: *p = g_rtab[RT_REMAP + (t << 8 | *p)]; break;
    case PIX_TBL: *p = g_rtab[RT_TBL + (t << 8 | *p)]; break;
    case PIX_TBL_REV: *p = g_rtab[RT_TBL + (*p << 8 | t)]; break;
    case PIX_GLOW: *p = g_rtab[RT_GLOW + (t << 8 | *p)]; break;
    }
}

/* Tbl_KeySave 0x4438c0 / Tbl_KeyRestore 0x4438f0: around the reversed-translucency fillers, the tbl column
   for texel 0xff is saved and set to the destination itself, so that texel is see-through. */
static void tbl_key_save(uint8_t save[256])
{
    for (int r = 0; r < 256; r++) save[r] = g_rtab[RT_TBL + (r << 8 | 0xff)], g_rtab[RT_TBL + (r << 8 | 0xff)] = (uint8_t)r;
}
static void tbl_key_restore(const uint8_t save[256])
{
    for (int r = 0; r < 256; r++) g_rtab[RT_TBL + (r << 8 | 0xff)] = save[r];
}

static void tex_affine(const Surface *s, ScreenVert *v, int n, const Texture *tex, int op)
{
    int next[64], prev[64];
    if (n < 1 || n > 64 || tex->h < 1 || tex->h > TEX_ROWS_MAX) return;
    tex_rows_build(tex);
    double su = MUL(SUB((double)tex->w, 1.0), 65536.0), sv = MUL(SUB((double)tex->h, 1.0), 65536.0);
    int top = 0, ymin = 100000;
    for (int i = 0; i < n; i++) {
        next[i] = i + 1 < n ? i + 1 : 0;
        prev[i] = i ? i - 1 : n - 1;
        int32_t y = round_magic((double)v[i].y);
        v[i].u = F32(MUL(v[i].u, su));
        v[i].v = F32(MUL(v[i].v, sv));
        if (y <= ymin) ymin = y, top = i;   /* ties: the later vertex */
    }
    uint8_t *row = s->pixels + (s->y0 + ymin) * s->pitch + s->x0;
    int l = top, r = top, y = ymin;
    TexEdge L = {0}, R = {0};
    for (;;) {
        if (round_magic((double)v[l].y) <= y) {   /* the left edge, along next; done when it is back at the top */
            int a;
            do {
                a = l;
                l = next[a];
                if (l == top) return;
            } while (round_magic((double)v[l].y) <= y);
            edge_setup_tex(&v[a], &v[l], &L);
        }
        if (round_magic((double)v[r].y) <= y) {   /* the right edge, along prev */
            int a;
            do {
                a = r;
                r = prev[a];
            } while (round_magic((double)v[r].y) <= y);
            edge_setup_tex(&v[a], &v[r], &R);
        }
        int xl = round_magic((double)L.x), xr = round_magic((double)R.x);
        if (xl != xr) {
            double inv = DIV(1.0, SUB(R.x, L.x));
            double du = MUL(SUB(R.u, L.u), inv), dv = MUL(SUB(R.v, L.v), inv);
            const TexEdge *e = xl < xr ? &L : &R;   /* the span starts at the left one of the two */
            int x1 = xl < xr ? xl : xr, x2 = xl < xr ? xr : xl;
            double t = SUB((double)x1, e->x);
            /* the start is the edge's coordinate rounded to an integer first, then the step times t added */
            uint32_t uu = (uint32_t)magic_lo(ADD(MUL(du, t), ADD(MAGIC, e->u)));
            uint32_t vv = (uint32_t)magic_lo(ADD(MUL(dv, t), ADD(MAGIC, e->v)));
            uint32_t ust = (uint32_t)magic_lo(ADD(du, MAGIC)), vst = (uint32_t)magic_lo(ADD(dv, MAGIC));
            uint8_t *p = row + x1;
            for (int k = x2 - x1; k > 0; k--) {
                tex_put(p++, op, *tex_texel(tex, (int32_t)vv >> 16, (int32_t)uu >> 16));
                uu += ust, vv += vst;
            }
        }
        L.x = F32(ADD(L.x, L.dx)), L.u = F32(ADD(L.u, L.du)), L.v = F32(ADD(L.v, L.dv));
        R.x = F32(ADD(R.x, R.dx)), R.u = F32(ADD(R.u, R.du)), R.v = F32(ADD(R.v, R.dv));
        row += s->pitch;
        y++;
    }
}

/* An edge of the perspective textured filler: x, w (1/z), u * w and v * w at the start vertex's rounded y and
   their steps per row. Here the steps are rounded to floats first and the start values computed from them. */
typedef struct { float x, w, u, v, dx, dw, du, dv; } PerspEdge;

static void edge_setup_persp(const ScreenVert *a, const ScreenVert *b, PerspEdge *e)
{
    double inv = DIV(1.0, SUB(b->y, a->y));
    e->dv = F32(MUL(SUB(b->v, a->v), inv));
    e->du = F32(MUL(SUB(b->u, a->u), inv));
    e->dw = F32(MUL(SUB(b->w, a->w), inv));
    e->dx = F32(MUL(inv, SUB(b->x, a->x)));
    double t = SUB((double)round_magic((double)a->y), a->y);
    e->w = F32(ADD(MUL(e->dw, t), a->w));
    e->u = F32(ADD(MUL(e->du, t), a->u));
    e->v = F32(ADD(MUL(e->dv, t), a->v));
    e->x = F32(ADD(MUL(t, e->dx), a->x));
}

static void edge_step_persp(PerspEdge *e)
{
    e->w = F32(ADD(e->w, e->dw)), e->v = F32(ADD(e->v, e->dv)), e->u = F32(ADD(e->u, e->du));
    e->x = F32(ADD(e->x, e->dx));
}

static void tex_persp(const Surface *s, ScreenVert *v, int n, const Texture *tex, int op)
{
    int next[64], prev[64];
    if (n < 1 || n > 64 || tex->h < 1 || tex->h > TEX_ROWS_MAX) return;
    tex_rows_build(tex);
    /* here only times (size - 1): the 65536 comes in with the reciprocal of w */
    double sv = SUB((double)tex->h, 1.0), su = SUB((double)tex->w, 1.0);
    int top = 0, ymin = 100000;
    for (int i = 0; i < n; i++) {
        next[i] = i + 1 < n ? i + 1 : 0;
        prev[i] = i ? i - 1 : n - 1;
        int32_t y = round_magic((double)v[i].y);
        v[i].u = F32(MUL(v[i].u, su));
        v[i].v = F32(MUL(v[i].v, sv));
        if (y <= ymin) ymin = y, top = i;   /* ties: the later vertex */
    }
    uint8_t *row = s->pixels + (s->y0 + ymin) * s->pitch + s->x0;
    int l = top, r = top, y = ymin;
    PerspEdge L = {0}, R = {0};
    for (;;) {
        if (round_magic((double)v[l].y) <= y) {   /* the left edge, along next; done when it is back at the top */
            int a;
            do {
                a = l;
                l = next[a];
                if (l == top) return;
            } while (round_magic((double)v[l].y) <= y);
            edge_setup_persp(&v[a], &v[l], &L);
        }
        if (round_magic((double)v[r].y) <= y) {   /* the right edge, along prev */
            int a;
            do {
                a = r;
                r = prev[a];
            } while (round_magic((double)v[r].y) <= y);
            edge_setup_persp(&v[a], &v[r], &R);
        }
        int xl = round_magic((double)L.x), xr = round_magic((double)R.x);
        if (xl != xr) {
            /* the slopes from the left end to the right one, or from the right end when they are swapped (the
               same values but for the rounding of the reciprocal) */
            const PerspEdge *e = xl < xr ? &L : &R, *f = xl < xr ? &R : &L;
            int x1 = xl < xr ? xl : xr, count = (xl < xr ? xr : xl) - x1;
            double inv = DIV(1.0, SUB(f->x, e->x));
            float dv = F32(MUL(SUB(f->v, e->v), inv)), du = F32(MUL(SUB(f->u, e->u), inv));
            float dw = F32(MUL(SUB(f->w, e->w), inv));
            double t = SUB((double)x1, e->x);
            double uw = ADD(MUL(du, t), e->u), ww = ADD(MUL(dw, t), e->w), vw = ADD(MUL(dv, t), e->v);
            float du16 = F32(MUL(du, 16.0)), dv16 = F32(MUL(dv, 16.0)), dw16 = F32(MUL(dw, 16.0));
            /* runs of 16 pixels, affine between exact points; the next run's reciprocal is started ahead */
            double rcp = DIV(65536.0, ww);
            uint32_t uu = (uint32_t)magic_lo(ADD(MUL(rcp, uw), MAGIC));
            uint32_t vv = (uint32_t)magic_lo(ADD(MUL(vw, rcp), MAGIC));
            ww = ADD(ww, dw16);
            rcp = DIV(65536.0, ww);
            uint8_t *p = row + x1;
            int left = count - 1;   /* the last pixel is drawn on its own, at the end */
            for (; left >= 16; left -= 16) {
                uw = ADD(uw, du16), vw = ADD(vw, dv16);
                uint32_t ue = (uint32_t)magic_lo(ADD(MUL(uw, rcp), MAGIC));
                uint32_t ve = (uint32_t)magic_lo(ADD(MUL(rcp, vw), MAGIC));
                ww = ADD(ww, dw16);
                rcp = DIV(65536.0, ww);
                uint32_t ust = (uint32_t)((int32_t)(ue - uu) >> 4), vst = (uint32_t)((int32_t)(ve - vv) >> 4);
                for (int k = 0; k < 16; k++) {
                    tex_put(p++, op, *tex_texel(tex, (int32_t)vv >> 16, (int32_t)uu >> 16));
                    uu += ust, vv += vst;
                }
                uu = ue, vv = ve;
            }
            if (left > 0) {   /* the rest: to the exact point `left` pixels on, the step through 1 / left */
                float rf = (float)left, rinv = (float)(1.0 / left);
                double we = ADD(SUB(ww, dw16), MUL(dw, rf));
                double ue = ADD(uw, MUL(du, rf)), ve = ADD(vw, MUL(dv, rf));
                double rc = DIV(65536.0, we);
                double uex = MUL(ue, rc), vex = MUL(rc, ve);
                uint32_t ust = (uint32_t)magic_lo(ADD(MUL(SUB(uex, (double)(int32_t)uu), rinv), MAGIC));
                uint32_t vst = (uint32_t)magic_lo(ADD(MUL(SUB(vex, (double)(int32_t)vv), rinv), MAGIC));
                for (int k = 0; k < left; k++) {
                    tex_put(p++, op, *tex_texel(tex, (int32_t)vv >> 16, (int32_t)uu >> 16));
                    uu += ust, vv += vst;
                }
            }
            tex_put(p, op, *tex_texel(tex, (int32_t)vv >> 16, (int32_t)uu >> 16));
        }
        edge_step_persp(&R);
        edge_step_persp(&L);
        row += s->pitch;
        y++;
    }
}

/* The reversed-translucency variants call Tbl_KeySave first, which clobbers the register holding the last
   texture's address: their comparison never matches and they rebuild the row table every time. */
static void tex_affine_rev(const Surface *s, ScreenVert *v, int n, const Texture *tex)
{
    uint8_t save[256];
    tbl_key_save(save);
    tex_rows_of = NULL;
    tex_affine(s, v, n, tex, PIX_TBL_REV);
    tbl_key_restore(save);
}
static void tex_persp_rev(const Surface *s, ScreenVert *v, int n, const Texture *tex)
{
    uint8_t save[256];
    tbl_key_save(save);
    tex_rows_of = NULL;
    tex_persp(s, v, n, tex, PIX_TBL_REV);
    tbl_key_restore(save);
}

/* Poly_FillTexAffine 0x44a570 */
void Poly_FillTexAffine(const Surface *s, ScreenVert *v, int n, const Texture *tex) { tex_affine(s, v, n, tex, PIX_COPY); }
/* Poly_FillTexRemap 0x4470b0 */
void Poly_FillTexRemap(const Surface *s, ScreenVert *v, int n, const Texture *tex) { tex_affine(s, v, n, tex, PIX_REMAP); }
/* Poly_FillTexTbl 0x442d90 */
void Poly_FillTexTbl(const Surface *s, ScreenVert *v, int n, const Texture *tex) { tex_affine(s, v, n, tex, PIX_TBL); }
/* Poly_FillTexTblRev 0x443320 */
void Poly_FillTexTblRev(const Surface *s, ScreenVert *v, int n, const Texture *tex) { tex_affine_rev(s, v, n, tex); }
/* Poly_FillTexGlow 0x4424e0 */
void Poly_FillTexGlow(const Surface *s, ScreenVert *v, int n, const Texture *tex) { tex_affine(s, v, n, tex, PIX_GLOW); }
/* Poly_FillTexPersp 0x44aaf0 */
void Poly_FillTexPersp(const Surface *s, ScreenVert *v, int n, const Texture *tex) { tex_persp(s, v, n, tex, PIX_COPY); }
/* Poly_FillTexPerspRemap 0x447640 */
void Poly_FillTexPerspRemap(const Surface *s, ScreenVert *v, int n, const Texture *tex) { tex_persp(s, v, n, tex, PIX_REMAP); }
/* Poly_FillTexPerspTbl 0x443910 */
void Poly_FillTexPerspTbl(const Surface *s, ScreenVert *v, int n, const Texture *tex) { tex_persp(s, v, n, tex, PIX_TBL); }
/* Poly_FillTexPerspTblRev 0x444150 */
void Poly_FillTexPerspTblRev(const Surface *s, ScreenVert *v, int n, const Texture *tex) { tex_persp_rev(s, v, n, tex); }
