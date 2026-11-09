/* The 8-bit drawing surface and the colour tables the rasterizer reads (docs/re/render.md).

   The original's surface record: +0 pitch (bytes per row, also the width), +4 the height, +0x10 the
   pixels, +0x18 left, +0x1c top, +0x20 right, +0x24 bottom (the viewport, inclusive), +0x28 nonzero when it
   can be drawn on. A row y,
   column x is pixels[(y + top) * pitch + left + x]: the fillers draw relative to the viewport. The line
   rasterizer clips against the same rectangle in absolute coordinates and then adds left and top again
   (the same when the viewport is at 0, 0, as the game's is). The tables are in g_rtab below. */
#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int pitch;
    uint8_t *pixels; /* +0x10 */
    int x0, y0;      /* +0x18, +0x1c: left, top */
    int x1, y1;      /* +0x20, +0x24: right, bottom (inclusive) */
    int on;          /* +0x28 */
    int w, h;        /* the buffer's size; h is the original's +4 (its +0, the pitch, is the width) */
} Surface;

/* A screen-space vertex as the polygon fillers take them (24 bytes in the original): x, y in pixels, w = 1/z
   (+8), the texture coordinates u, v (+0xc, +0x10; times w for the perspective fillers), and q (+0x14), the
   shade term. */
typedef struct {
    float x, y;
    float w;         /* +8 */
    float u, v;      /* +0xc, +0x10 */
    float q;         /* +0x14 */
} ScreenVert;

/* A texture as the textured fillers take it: u32 width, u32 height, then the 8-bit pixels row by row. */
typedef struct {
    int32_t w, h;
    uint8_t pixels[];
} Texture;

/* The colour tables, one block as in the original (0x5af150-0x5e0450), since the fillers' indices can run
   from one table into the next (the glow table's rows past 15 are the translucency table's first rows):
     +0x00000 (0x5af150) glow: 16 rows of 256, [intensity << 8 | destination], built from the palette at start
              (0x43b690)
     +0x01000 (0x5b0150) tbl: the world's .tbl, translucency [texel << 8 | destination]
     +0x11000 (0x5c0150) 256 bytes 0..255, +0x11100 (0x5c0250) 256 bytes of a colour, then 256 more
     +0x11300 (0x5c0450) lum: the world's .lum, [shade << 8 | colour]
     +0x21300 (0x5d0450) remap: [texel << 8 | destination]; row t is all t (opaque) until a colour is keyed
              (its row becomes the destination: see-through) or remapped (0x43b5a0, 0x43c100, 0x43c0c0) */
enum { RT_GLOW = 0x0, RT_TBL = 0x1000, RT_LUM = 0x11300, RT_REMAP = 0x21300, RT_SIZE = 0x31300 };
extern uint8_t g_rtab[RT_SIZE];

/* The port's seam for its GL backend (render/gl_poly.h, openi76-gl.wasm; not the original's): the 2D
   drawing tells it the rectangles it drew on a surface (absolute pixels, inclusive), which then show the
   software's pixels. NULL without the backend. */
extern void (*g_surface_drawn)(const Surface *s, int x0, int y0, int x1, int y1);
static inline void surface_drawn(const Surface *s, int x0, int y0, int x1, int y1)
{
    if (g_surface_drawn) g_surface_drawn(s, x0, y0, x1, y1);
}
/* the same for len bytes from the pixels' start + at, by rows */
static inline void surface_drawn_span(const Surface *s, size_t at, size_t len)
{
    if (!g_surface_drawn || !len || s->pitch <= 0) return;
    int p = s->pitch, y0 = (int)(at / (size_t)p), y1 = (int)((at + len - 1) / (size_t)p);
    int x0 = (int)(at % (size_t)p), x1 = (int)((at + len - 1) % (size_t)p);
    if (y0 == y1) { g_surface_drawn(s, x0, y0, x1, y0); return; }
    g_surface_drawn(s, x0, y0, p - 1, y0);
    if (y1 > y0 + 1) g_surface_drawn(s, 0, y0 + 1, p - 1, y1 - 1);
    g_surface_drawn(s, 0, y1, x1, y1);
}
#define g_lum (g_rtab + RT_LUM)
