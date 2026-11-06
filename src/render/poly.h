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
