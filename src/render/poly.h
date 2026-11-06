/* Polygon fillers (docs/re/render.md). */
#pragma once
#include "render/surface.h"

/* Poly_FillFlat 0x43ea00: a convex polygon of n screen vertices in one colour, shaded once for the whole
   polygon from vertex 0's q through the lum table. */
void Poly_FillFlat(const Surface *s, const ScreenVert *v, int n, uint8_t colour);

/* Poly_FillGouraud 0x43ecd0: a convex polygon in one colour, the shade interpolated from each vertex's q
   through the lum table. Rewrites every vertex's q as its shade in 16.16 ((1 - q) * 7936 * 256), as the
   original does to its caller's vertices. */
void Poly_FillGouraud(const Surface *s, ScreenVert *v, int n, uint8_t colour);

/* Poly_FillTexAffine 0x44a570 (and at 0x449640 and 0x446180, modes 0x05 and 0x45: the same code, no shading
   and no remap): a convex polygon textured with tex, u and v (0..1 across the texture)
   interpolated linearly in screen space, no shading. Scales every vertex's u by (w - 1) * 65536 and v by
   (h - 1) * 65536 in place, as the original does to its caller's vertices. */
void Poly_FillTexAffine(const Surface *s, ScreenVert *v, int n, const Texture *tex);

/* Poly_FillTexPersp 0x44aaf0: as Poly_FillTexAffine with perspective-correct u, v (the vertices carry u * w,
   v * w and w = 1/z): exact at every 16th pixel and at the span's end, linear in between. Scales the
   vertices' u by (w - 1) and v by (h - 1) in place. */
void Poly_FillTexPersp(const Surface *s, ScreenVert *v, int n, const Texture *tex);

/* The same two fillers with another pixel (t the texel, d the destination; tables in surface.h):
   Remap 0x4470b0 / 0x447640 (modes 0x44, 0x54): remap[t << 8 | d], opaque but for keyed colours.
   Tbl 0x442d90 / 0x443910 (0x84, 0x94): tbl[t << 8 | d], translucent.
   TblRev 0x443320 / 0x444150 (0xa4, 0xb4): tbl[d << 8 | t], with texel 0xff see-through for the call.
   Glow 0x4424e0 (0xc4, affine only): glow[t << 8 | d], t an intensity 0..15. */
void Poly_FillTexRemap(const Surface *s, ScreenVert *v, int n, const Texture *tex);
void Poly_FillTexTbl(const Surface *s, ScreenVert *v, int n, const Texture *tex);
void Poly_FillTexTblRev(const Surface *s, ScreenVert *v, int n, const Texture *tex);
void Poly_FillTexGlow(const Surface *s, ScreenVert *v, int n, const Texture *tex);
void Poly_FillTexPerspRemap(const Surface *s, ScreenVert *v, int n, const Texture *tex);
void Poly_FillTexPerspTbl(const Surface *s, ScreenVert *v, int n, const Texture *tex);
void Poly_FillTexPerspTblRev(const Surface *s, ScreenVert *v, int n, const Texture *tex);

/* Tiled (mode bit 1): u, v scaled by the size, not size - 1, and the texel indices masked with size - 1.
   Affine 0x448870 (modes 0x06, and 0x46 at 0x4453a0, the same code); perspective 0x448e00 (0x16) and
   0x445930 (0x56: texel 0xff see-through, not the remap). */
void Poly_FillTexTiled(const Surface *s, ScreenVert *v, int n, const Texture *tex);

/* The tiled affine filler's bug: it never stores the texture's height in the stack slot its row-table build
   reads it from, so a new texture's four copies of the last row go after row `stale` instead, and rows
   stale..stale + 3 repeat row stale - 1. The slot is the one the filler keeps its row counter in, so it
   holds the last row of the previous polygon this filler drew: the port keeps that here. (In the original,
   any other code that ran at that stack depth in between could have left another value.) */
extern int g_poly_stale_row;
void Poly_FillTexPerspTiled(const Surface *s, ScreenVert *v, int n, const Texture *tex);
void Poly_FillTexPerspTiledKey(const Surface *s, ScreenVert *v, int n, const Texture *tex);
