/* The line rasterizer: integer Cohen-Sutherland clipping and Bresenham stepping through a two-entry table. */
#include "render/line.h"
#include <stdint.h>

/* The outcode: 8 left of x0, 4 right of x1, 2 above y0, 1 below y1. Each bit is the sign of a 32-bit
   difference (add, adc), so it wraps as the original's does for coordinates far apart. */
static int outcode(const Surface *s, int x, int y)
{
    int c = 0;
    c = c << 1 | (int)(((uint32_t)x - (uint32_t)s->x0) >> 31);
    c = c << 1 | (int)(((uint32_t)x - 1u - (uint32_t)s->x1) >> 31);
    c = c << 1 | (int)(((uint32_t)y - (uint32_t)s->y0) >> 31);
    c = c << 1 | (int)(((uint32_t)y - 1u - (uint32_t)s->y1) >> 31);
    return c ^ 5;
}

/* imul, idiv: the 64-bit product divided, truncated */
static int muldiv(int a, int b, int c) { return (int)((int64_t)a * b / c); }

/* Line_Draw 0x44b310 */
void Line_Draw(const Surface *s, int x0, int y0, int x1, int y1, uint8_t colour, int test)
{
    if (!s->on) return;
    int ca = outcode(s, x0, y0), cb = outcode(s, x1, y1);
    while (ca | cb) {
        if (ca & cb) return;
        int c = ca ? ca : cb, x, y;
        if (c & 8) x = s->x0, y = y0 + muldiv(y1 - y0, s->x0 - x0, x1 - x0);
        else if (c & 4) x = s->x1, y = y0 + muldiv(y1 - y0, s->x1 - x0, x1 - x0);
        else if (c & 2) y = s->y0, x = x0 + muldiv(x1 - x0, s->y0 - y0, y1 - y0);
        else y = s->y1, x = x0 + muldiv(x1 - x0, s->y1 - y0, y1 - y0);
        if (c == ca) x0 = x, y0 = y, ca = outcode(s, x0, y0);
        else x1 = x, y1 = y, cb = outcode(s, x1, y1);
    }
    int dx = x1 - x0, dy = y1 - y0;
    uint8_t *p = s->pixels + (s->y0 + y0) * s->pitch + s->x0 + x0;
    if (test) return;
    int xs = dx < 0 ? -1 : 1, ys = dy < 0 ? -s->pitch : s->pitch, a = dx < 0 ? -dx : dx, b = dy < 0 ? -dy : dy;
    /* the major axis's count of pixels; the error's sign picks the step: [0] both axes, [1] the major one */
    int n, err, inc[2], step[2];
    if (a >= b) n = a, err = 2 * b - a, inc[0] = 2 * b - 2 * a, inc[1] = 2 * b, step[0] = ys + xs, step[1] = xs;
    else n = b, err = 2 * a - b, inc[0] = 2 * a - 2 * b, inc[1] = 2 * a, step[0] = xs + ys, step[1] = ys;
    for (; n > 0; n--) {
        int neg = (int)((uint32_t)err >> 31);
        err += inc[neg];
        *p = colour;
        p += step[neg];
    }
}
