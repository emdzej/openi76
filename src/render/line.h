/* Lines on the 8-bit surface (docs/re/render.md). */
#pragma once
#include "render/surface.h"

/* Line_Draw 0x44b310: a line from (x0, y0) towards (x1, y1), clipped to the surface's viewport, the end
   point not drawn. Nothing is drawn if the surface is off or if test is nonzero (the clipping still runs). */
void Line_Draw(const Surface *s, int x0, int y0, int x1, int y1, uint8_t colour, int test);
