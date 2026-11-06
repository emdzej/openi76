/* Polygon fillers. The originals are hand-written x87 assembly; the port keeps their arithmetic (the
   rounding through 1.5 * 2^52, the floats stored at the same points) so the pixels match, which
   tools/emu/diff_poly.py checks against the original machine code. */
#include "render/poly.h"
#include <math.h>
#include <string.h>

uint8_t g_lum[256 * 256];

/* fadd 6755399441055744.0 then the low dword: round to nearest, ties to even (the FPU's default). */
static int32_t round_magic(double x) { return (int32_t)(int64_t)nearbyint(x); }

/* One edge's start: x at the start vertex's rounded y, and the step per row (both stored as floats). */
static void edge_setup(const ScreenVert *a, const ScreenVert *b, float *x, float *step)
{
    double inv = 1.0 / ((double)b->y - (double)a->y);
    int32_t ya = round_magic((double)a->y);
    double slope = ((double)b->x - (double)a->x) * inv;
    *step = (float)slope;
    *x = (float)(((double)ya - (double)a->y) * slope + (double)a->x);
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
    int shade = round_magic((1.0 - (double)v[0].q) * 7936.0);
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
        lx = (float)((double)lx + (double)ls);
        rx = (float)((double)rx + (double)rs);
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
    double inv = 1.0 / ((double)b->y - (double)a->y);
    double t = (double)round_magic((double)a->y) - (double)a->y;
    *dx = (float)(((double)b->x - (double)a->x) * inv);
    *ds = (float)(((double)b->q - (double)a->q) * inv);
    *x = (float)((double)*dx * t + (double)a->x);
    *sh = (float)(t * (double)*ds + (double)a->q);
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
        v[i].q = (float)((1.0 - (double)v[i].q) * 7936.0 * 256.0);
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
            double q = ((double)rs - (double)ls) / ((double)rx - (double)lx);
            float qf = (float)q;
            double start;
            int x1, x2;
            if (xl < xr)
                start = ((double)xl - (double)lx) * (double)qf + (double)ls, x1 = xl, x2 = xr;
            else
                start = ((double)xr - (double)rx) * (double)qf + (double)rs, x1 = xr, x2 = xl;
            uint32_t sv = rol16(round_magic(start)), st = rol16(round_magic(q));
            uint8_t *p = row + x1;
            for (int k = x2 - x1; k > 0; k--) {
                *p++ = g_lum[(sv & 0xff) << 8 | colour];
                sv = add_endaround(sv, st);
            }
        }
        rx = (float)((double)rx + (double)rdx);
        rs = (float)((double)rds + (double)rs);
        lx = (float)((double)lx + (double)ldx);
        ls = (float)((double)lds + (double)ls);
        row += s->pitch;
        row_y++;
    }
}
