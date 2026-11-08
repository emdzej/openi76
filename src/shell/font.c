/* font.h: the shell's fonts and text */
#include "shell/font.h"
#include "shell/screen.h"
#include <string.h>

static int32_t rd(const uint8_t *p)
{
    int32_t v;
    memcpy(&v, p, 4);
    return v;
}

void Font_Init(ShFont *f, const uint8_t *data)
{
    f->data = data;
    f->height = data ? rd(data + 8) : 0;
}

int Font_Height(const ShFont *f) { return rd(f->data + 8); }

int Font_Width(const ShFont *f, uint8_t c) { return rd(f->data + rd(f->data + 0x10 + 4 * c)); }

int Text_Width(const ShFont *f, const char *s)
{
    int w = 0;
    for (; *s; s++) w += Font_Width(f, (uint8_t)*s);   /* the stride 0x10067d88: 1 (2 for a double-byte code page) */
    return w;
}

int Gfx_Char(const GfxView *v, int x, int y, const ShFont *f, uint8_t c, const uint8_t *map)
{
    const GfxBitmap *bm = v->bm;
    if (bm->wmax + 1 == 0 || bm->wmax < -1 || bm->hmax < 0) return -1;
    int wx0 = v->x0 < 1 ? 0 : v->x0, wy0 = v->y0 < 1 ? 0 : v->y0;
    int wx1 = bm->wmax <= v->x1 ? bm->wmax : v->x1, wy1 = bm->hmax <= v->y1 ? bm->hmax : v->y1;
    if (wx1 < wx0 || wy1 < wy0) return -2;
    x += v->x0, y += v->y0;
    int h = rd(f->data + 8);
    const uint8_t *g = f->data + rd(f->data + 0x10 + 4 * c);
    int w = rd(g), drawn = 0;
    if (!w) return 0;
    const uint8_t *src = g + 4;
    int cols = w, over = wx1 + 1 - w - x;
    if (over < 0 && (cols = w + over) <= 0) return w;   /* past the right */
    int left = x - wx0;
    if (left < 0) {
        if ((cols += left) <= 0) return w;
        src -= left, x -= left;
    }
    int rows = h, under = wy1 + 1 - h - y;
    if (under < 0 && (rows = h + under) <= 0) return w;
    int top = y - wy0;
    if (top < 0) {
        if ((rows += top) <= 0) return w;
        y -= top, src -= top * w;
    }
    int pitch = bm->wmax + 1;
    uint8_t *dst = bm->px + y * pitch + x;
    int skip = w - cols;
    if (!map) {   /* every byte; the width returned */
        for (; rows; rows--, src += skip, dst += pitch - cols)
            for (int i = 0; i < cols; i++) *dst++ = *src++;
        return skip + cols;
    }
    for (; rows; rows--, src += skip, dst += pitch - cols) {
        drawn = cols;
        for (int i = 0; i < cols; i++, src++, dst++)
            if (map[*src] != 0xff) *dst = map[*src];
    }
    return skip + drawn;   /* the original's: the glyph's width, or the cut part's only when no row was drawn */
}

int Scr_Text(int x, int y, const ShFont *f, const char *s, const uint8_t *map)
{
    if (!s) return 0;
    int w = Text_Width(f, s);
    if (!w) return 0;
    /* the clip test of 0x10067988 is off in the DLL's data */
    static uint8_t def[256];   /* 0x1009b0a8: 0 see-through, the rest themselves (0x1003c670) */
    if (!def[1])
        for (int i = 0; i < 256; i++) def[i] = i ? (uint8_t)i : 0xff;
    if (!map) map = def;
    for (int k = x; *s; s++) k += Gfx_Char(&g_shs.scr, k, y, f, (uint8_t)*s, map);
    Scr_Dirty(x, y, x + w - 1, y + Font_Height(f) - 1);
    return w;
}
