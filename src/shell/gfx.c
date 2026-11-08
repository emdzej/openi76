/* gfx.h: the shell's 2D library */
#include "shell/gfx.h"
#include <stdbool.h>
#include <string.h>

static int32_t rd32s(const uint8_t *p) { return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24); }

typedef struct { int x0, y0, x1, y1; } Clip;

/* the window within the bitmap; false for a bad bitmap */
static int clip_of(const GfxView *v, Clip *c)
{
    const GfxBitmap *b = v->bm;
    if (b->wmax + 1 == 0 || b->wmax < -1 || b->hmax < 0) return -1;
    c->x0 = v->x0 < 1 ? 0 : v->x0;
    c->y0 = v->y0 < 1 ? 0 : v->y0;
    c->x1 = b->wmax <= v->x1 ? b->wmax : v->x1;
    c->y1 = b->hmax <= v->y1 ? b->hmax : v->y1;
    return 0;
}

int Gfx_Frames(const uint8_t *set) { return rd32s(set + 4); }

/* A frame's record: +8 x0, +0xc y0, +0x10 x1, +0x14 y1, then from +0x18 its rows, y0 to y1, each a run
   of codes: a byte c and n = c >> 1; c even with n, n pixels of the next byte; c odd with n, the next n
   bytes; c odd without n, the next byte's count of pixels left as they are; c even without n, the row's
   end. Clipped pixel by pixel (the original's unclipped path, 0x1002df28, when it is all inside) */
int Gfx_Frame(const GfxView *v, const uint8_t *set, int k, int x, int y)
{
    Clip c;
    if (clip_of(v, &c)) return -1;
    if (c.x1 < c.x0 || c.y1 < c.y0) return -2;
    int px = x + v->x0, py = y + v->y0;
    const uint8_t *f = set + rd32s(set + 8 + 8 * k);
    int fx0 = rd32s(f + 8) + px, fy0 = rd32s(f + 12) + py, fx1 = rd32s(f + 16) + px, fy1 = rd32s(f + 20) + py;
    if (fx1 < fx0 || fy1 < fy0) return -4;
    int o0 = ((((fx0 - c.x0 < 0) * 2 + (c.x1 - fx0 < 0)) * 2 + (fy0 - c.y0 < 0)) * 2) + (c.y1 - fy0 < 0);
    int o1 = ((((fx1 - c.x0 < 0) * 2 + (c.x1 - fx1 < 0)) * 2 + (fy1 - c.y0 < 0)) * 2) + (c.y1 - fy1 < 0);
    if (o0 & o1) return -3;
    int pitch = v->bm->wmax + 1;
    const uint8_t *p = f + 0x18;
    for (int row = fy0; row <= fy1; row++) {
        if (row > c.y1) return 0;
        bool vis = row >= c.y0;
        uint8_t *line = v->bm->px + (size_t)row * (size_t)pitch;
        int cx = fx0;
        for (;;) {
            int b = *p++, n = b >> 1;
            if (b & 1) {
                if (!n) {
                    cx += *p++;
                    continue;
                }
                for (int i = 0; i < n; i++, cx++)
                    if (vis && cx >= c.x0 && cx <= c.x1) line[cx] = p[i];
                p += n;
            } else {
                if (!n) break;
                uint8_t val = *p++;
                for (int i = 0; i < n; i++, cx++)
                    if (vis && cx >= c.x0 && cx <= c.x1) line[cx] = val;
            }
        }
    }
    return 0;
}

/* 0x100317ce: n bytes at the view's row (its origin's x), clipped */
static int pcx_row(const GfxView *v, int row, const uint8_t *src, int n)
{
    Clip c;
    if (clip_of(v, &c)) return -1;
    if (c.x1 < c.x0 || c.y1 < c.y0) return -2;
    int y = row + v->y0, x = v->x0;
    int over = c.x1 - x + 1 - n;
    if (over < 0) n += over;
    if (n <= 0) return 0;
    if (x < c.x0) {
        int skip = c.x0 - x;
        n -= skip;
        if (n <= 0) return 0;
        src += skip, x += skip;
    }
    if (y > c.y1 || y < c.y0) return 0;
    memcpy(v->bm->px + (size_t)y * (size_t)(v->bm->wmax + 1) + x, src, (size_t)n);
    return 0;
}

/* The header's y0 (+6), y1 (+0xa) and bytes per line (+0x42); the rows from +0x80, each decoded into a
   line buffer (0x1006527d) until it holds the bytes per line: a byte with its two high bits set is a
   run of its low six of the next byte, else a pixel. A run past the line is lost with it */
int Gfx_Pcx(const GfxView *v, const uint8_t *pcx, uint32_t n)
{
    static uint8_t line[0x10000 + 64];
    int rows = (uint16_t)((uint16_t)(pcx[10] | pcx[11] << 8) - (uint16_t)(pcx[6] | pcx[7] << 8));
    int bpl = pcx[0x42] | pcx[0x43] << 8;
    uint32_t at = 0x80;
    for (int r = 0; r <= rows; r++) {
        int o = 0;
        while (o < bpl) {
            uint8_t b = at < n ? pcx[at] : 0;
            if ((b & 0xc0) == 0xc0) {
                uint8_t val = at + 1 < n ? pcx[at + 1] : 0;
                at += 2;
                for (int k = b & 0x3f; k; k--)
                    if (o < (int)sizeof line) line[o++] = val;
            } else {
                at++;
                line[o++] = b;
            }
        }
        pcx_row(v, r, line, bpl);
    }
    return 0;
}

int Gfx_Copy(const GfxView *src, int sx, int sy, const GfxView *dst, int dx, int dy, int32_t fill)
{
    Clip s, d;
    if (clip_of(src, &s)) return -1;
    if (s.x1 < s.x0 || s.y1 < s.y0) return -2;
    /* the source's part in its window's coordinates */
    s.x0 -= src->x0, s.x1 -= src->x0, s.y0 -= src->y0, s.y1 -= src->y0;
    if (clip_of(dst, &d)) return -1;
    if (d.x1 < d.x0 || d.y1 < d.y0) return -2;
    d.x0 -= dst->x0, d.x1 -= dst->x0, d.y0 -= dst->y0, d.y1 -= dst->y0;
    int ox = sx - dx, oy = sy - dy;   /* a source point less this is its destination */
    int ux0 = s.x0 > d.x0 + ox ? s.x0 : d.x0 + ox, uy0 = s.y0 > d.y0 + oy ? s.y0 : d.y0 + oy;
    int ux1 = s.x1 < d.x1 + ox ? s.x1 : d.x1 + ox, uy1 = s.y1 < d.y1 + oy ? s.y1 : d.y1 + oy;
    if (ux1 < ux0 || uy1 < uy0) return -3;
    int sp = src->bm->wmax + 1, dp = dst->bm->wmax + 1, w = ux1 - ux0 + 1;
    /* bottom up when the rows move down within one bitmap, as the original's direction for overlaps */
    bool up = src->bm == dst->bm && dst->y0 - oy > src->y0;
    for (int k = 0; k <= uy1 - uy0; k++) {
        int u = up ? uy1 - k : uy0 + k;
        uint8_t *to = dst->bm->px + (size_t)(dst->y0 + u - oy) * (size_t)dp + dst->x0 + ux0 - ox;
        if (!(fill & ~0xff)) memset(to, fill, (size_t)w);
        else memmove(to, src->bm->px + (size_t)(src->y0 + u) * (size_t)sp + src->x0 + ux0, (size_t)w);
    }
    return 0;
}

int Gfx_Fill(const GfxView *v, uint8_t c)
{
    Clip k;
    if (clip_of(v, &k)) return -1;
    if (k.x1 < k.x0 || k.y1 < k.y0) return -2;
    int pitch = v->bm->wmax + 1;
    for (int y = k.y0; y <= k.y1; y++) memset(v->bm->px + (size_t)y * (size_t)pitch + k.x0, c, (size_t)(k.x1 - k.x0 + 1));
    return 0;
}
