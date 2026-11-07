/* Surfaces of bitmaps and the scaled sprite blitter (sprite.h). */
#include "render/sprite.h"
#include "render/view.h"
#include <stdlib.h>
#include <string.h>

Surface Surface_Make(int w, int h, uint8_t *pixels)
{
    Surface s = {0};
    if ((int32_t)((uint32_t)w * (uint32_t)h) < 1) return s;
    if (!pixels && !(pixels = calloc((size_t)w * (size_t)h, 1))) return s;   /* HeapAlloc, aligned to 32 */
    s.pitch = w;
    s.pixels = pixels;
    s.x1 = w - 1, s.y1 = h - 1;
    s.on = 1;
    s.w = w, s.h = h;
    return s;
}

/* The blitter steps the source in 16.16: a column's source x is the integer part of the clipped columns'
   offset plus one more step (the first column already one step in), a row's the offset without (the first
   row at the clip's own). Its x steps are the original's EDX:EAX pair, 48 bits; its y steps a 32-bit sum
   whose carry moves a row. */
void Sprite_Draw(Surface *dst, const Surface *src, int x0, int y0, int x1, int y1, int mode, int colour)
{
    if (!dst->on || !src->on) return;
    if (x1 < x0) { int t = x0; x0 = x1; x1 = t; }
    if (y1 < y0) { int t = y0; y0 = y1; y1 = t; }
    int32_t sx = 0, sy = 0;   /* the steps */
    if (x1 - x0) sx = (int32_t)((uint32_t)(src->x1 - src->x0) << 16) / (x1 - x0);
    if (y1 - y0) sy = (int32_t)((uint32_t)(src->y1 - src->y0) << 16) / (y1 - y0);
    int r[4] = {dst->x0 + x0, dst->y0 + y0, dst->x0 + x1, dst->y0 + y1};
    int c[4] = {dst->x0, dst->y0, dst->x1, dst->y1};
    if (Rect_Clip(r, c) < 0) return;
    int skip_x = r[0] - dst->x0 - x0, skip_y = r[1] - dst->y0 - y0;
    int w = r[2] - r[0] + 1, h = r[3] - r[1] + 1;
    uint8_t *d = dst->pixels + (intptr_t)r[1] * dst->pitch + r[0];
    if (mode != 3) return;   /* 1 (0x43e6e0), 2, 4 and 5: not ported yet */
    static uint32_t xs[2048];   /* the original's buffer on its stack */
    uint32_t ax = 0, ay = 0;
    for (int k = 0; k < skip_x; k++) ax += (uint32_t)sx;
    for (int k = 0; k < skip_y; k++) ay += (uint32_t)sy;
    uint64_t x = (uint64_t)(ax >> 16) << 32 | (uint64_t)(ax << 16);
    uint64_t step = (uint64_t)((uint32_t)sx >> 16) << 32 | (uint64_t)((uint32_t)sx << 16);
    for (int k = 0; k < w && k < 2048; k++) {
        x += step;
        xs[k] = (uint32_t)(x >> 32);
    }
    const uint8_t *s = src->pixels + (intptr_t)(((int32_t)ay >> 16) + src->y0) * src->pitch + src->x0;
    intptr_t row[2] = {(intptr_t)(sy >> 16) * src->pitch, (intptr_t)(sy >> 16) * src->pitch + src->pitch};
    uint32_t fy = ay << 16, dy = (uint32_t)sy << 16;
    for (; h; h--) {
        for (int k = 0; k < w; k++) {
            uint8_t t = s[xs[k]];
            if (t == 0xff) continue;
            uint32_t i = (xs[k] & 0xffff0000u) | (uint32_t)(uint8_t)(colour - d[k]) << 8 | t;
            d[k] = g_lum[i];
        }
        d += dst->pitch;
        uint32_t f = fy + dy;
        s += row[f < fy];
        fy = f;
    }
}

void Surface_Blit(Surface *dst, const Surface *src, int x, int y, int mode)
{
    if (!dst->on || !src->on) return;
    int r[4] = {dst->x0 + x, dst->y0 + y, 0, 0};
    r[2] = src->x1 - src->x0 + r[0];
    r[3] = src->y1 - src->y0 + r[1];
    const int c[4] = {dst->x0, dst->y0, dst->x1, dst->y1};
    if (Rect_Clip(r, c) < 0) return;
    int sx = src->x0 - dst->x0 - x + r[0], sy = src->y0 - dst->y0 - y + r[1];
    int w = r[2] - r[0] + 1, h = r[3] - r[1] + 1;
    uint8_t *d = dst->pixels + r[1] * dst->pitch + r[0];
    const uint8_t *s = src->pixels + src->pitch * sy + sx;
    int step = src->pitch;
    const uint8_t *t;
    switch (mode) {
    case 1: t = g_rtab + RT_REMAP; break;
    case 2: t = g_rtab + RT_TBL; break;
    /* mode 4: the table 0x5c0150 for rows of 8 or more, the remap table for narrower ones (quirk, kept) */
    case 4: t = g_rtab + (w < 8 ? RT_REMAP : 0x11000); break;
    case 5:   /* mode 1 with the source's rows read bottom up */
        t = g_rtab + RT_REMAP;
        s = src->pixels + (src->h - sy - 1) * src->pitch + sx;
        step = -src->pitch;
        break;
    default: t = NULL; break;
    }
    for (; h > 0; h--, d += dst->pitch, s += step)
        if (t)
            for (int i = 0; i < w; i++) d[i] = t[(unsigned)s[i] << 8 | d[i]];
        else
            memcpy(d, s, (size_t)w);
}

void Surface_FillRect(Surface *s, uint8_t c)
{
    if (!s->on) return;
    uint8_t *p = s->pixels + s->pitch * s->y0 + s->x0;
    int w = s->x1 - s->x0 + 1;
    for (int h = s->y1 - s->y0 + 1; h > 0; h--, p += s->pitch) memset(p, c, (size_t)w);
}
