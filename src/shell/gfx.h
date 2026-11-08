/* The shell's 2D library (the "aw" sources linked into I76SHELL.DLL, 0x1002d000-0x10032000): bitmaps of
   8-bit pixels, views on them, and the frame sets' RLE frames, PCX pictures, copies and fills drawn
   through the views. */
#pragma once
#include <stdint.h>

typedef struct {
    uint8_t *px;    /* +0 */
    int wmax;       /* +4: the width less one (the pitch is the width) */
    int hmax;       /* +8: the height less one */
} GfxBitmap;

typedef struct {    /* a view: a bitmap and a window on it, in its pixels: the window's corner is the */
    GfxBitmap *bm;  /* origin of what is drawn through it, and clips it with the bitmap's bounds */
    int x0, y0, x1, y1;
} GfxView;

/* aw 0x1002dab4 (0x1002df28 unclipped): frame k of a frame set (docs/formats/mw2.md) at x, y of the
   view's origin. 0 drawn; -1 a bad bitmap, -2 an empty window, -4 an empty frame, -3 all outside */
int Gfx_Frame(const GfxView *v, const uint8_t *set, int k, int x, int y);
/* aw 0x10031b44: a PCX picture's rows (RLE, its bytes per line each) through the view, row by row from
   its origin (0x100317ce clips each) */
int Gfx_Pcx(const GfxView *v, const uint8_t *pcx, uint32_t n);
/* aw 0x1002fa48: the source view's window into the destination's, the source's (sx, sy) at the
   destination's (dx, dy); fill 0-255 fills the destination's part with that colour instead, else
   (-1) copies */
int Gfx_Copy(const GfxView *src, int sx, int sy, const GfxView *dst, int dx, int dy, int32_t fill);
/* aw 0x1002f945: the view's window filled */
int Gfx_Fill(const GfxView *v, uint8_t c);
/* the frame set's count of frames (+4), and frame k's box (+8..+0x14 of its record: x0, y0, x1, y1 of
   the place it is drawn at) */
int Gfx_Frames(const uint8_t *set);
