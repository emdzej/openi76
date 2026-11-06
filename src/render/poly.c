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
