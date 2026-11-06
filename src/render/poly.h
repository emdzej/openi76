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

/* Poly_FillTexAffine 0x44a570: a convex polygon textured with tex, u and v (0..1 across the texture)
   interpolated linearly in screen space, no shading. Scales every vertex's u by (w - 1) * 65536 and v by
   (h - 1) * 65536 in place, as the original does to its caller's vertices. */
void Poly_FillTexAffine(const Surface *s, ScreenVert *v, int n, const Texture *tex);

/* Poly_FillTexPersp 0x44aaf0: as Poly_FillTexAffine with perspective-correct u, v (the vertices carry u * w,
   v * w and w = 1/z): exact at every 16th pixel and at the span's end, linear in between. Scales the
   vertices' u by (w - 1) and v by (h - 1) in place. */
void Poly_FillTexPersp(const Surface *s, ScreenVert *v, int n, const Texture *tex);
