/* Surfaces of bitmaps and the scaled sprite blitter (0x4391c0, 0x43df60-0x43e900). */
#pragma once
#include <stdint.h>
#include "render/surface.h"

/* Surface_Make 0x4391c0: a w x h surface over pixels (allocated when NULL), its viewport the whole of it;
   all zero when w * h < 1 */
Surface Surface_Make(int w, int h, uint8_t *pixels);

/* Sprite_Draw 0x43df60: src's viewport scaled onto dst's rectangle (x0, y0)-(x1, y1) (inclusive, relative
   to dst's viewport, either order), clipped to dst's viewport. Mode 3 draws src's texels but 0xff through
   the lum table, [(colour - destination) << 8 | texel]; the other modes (1, 2, 4, 5) are not ported yet and
   draw nothing. Nothing either when a surface is off. */
void Sprite_Draw(Surface *dst, const Surface *src, int x0, int y0, int x1, int y1, int mode, int colour);
