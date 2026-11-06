/* Polygon fillers (docs/re/render.md). */
#pragma once
#include "render/surface.h"

/* Poly_FillFlat 0x43ea00: a convex polygon of n screen vertices in one colour, shaded once for the whole
   polygon from vertex 0's q through the lum table. */
void Poly_FillFlat(const Surface *s, const ScreenVert *v, int n, uint8_t colour);
