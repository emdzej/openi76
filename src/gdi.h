/* The parts of Windows' GDI the exe draws text with (the in-game menus, 0x44bc40-0x44d3b0, through a memory
   DC over an 8-bit DIB section): TrueType fonts (CreateFontIndirectA), TextOutA with a transparent
   background, GetTextExtentPoint32A, GetTextExtentExPointA and Rectangle.

   The fonts are TrueType files read through the vfs: the face "Lee" is the disc's LEE_____.TTF, which
   WinMain installs (Font_AddResource 0x439c80). Windows' own fonts are not on the discs: a face is
   looked for as <FACE>.TTF without spaces (ARIALBOLD.TTF) and the Windows file name (ARIALBD.TTF), and
   without either the port falls back to Lee (logged once).

   The glyphs are as GDI drew them in 1997, without antialiasing: a pixel is the text colour where the
   outline covers its centre (stb_truetype's coverage at half or more). GDI's hinting is not run, so the
   glyphs' shapes can differ by a pixel; the advances are the font's hinted ones from its hdmx table at
   the size, as GDI's are, and the font's own scaled and rounded ones without. A positive height is the
   cell's (ascent + descent, usWinAscent + usWinDescent), as for lfHeight > 0. */
#pragma once
#include "render/surface.h"
#include <stdint.h>

typedef struct GdiFont GdiFont;

/* CreateFontIndirectA with lfHeight height (> 0, the cell's), the face name; NULL when no file is found */
GdiFont *Gdi_Font(const char *face, int height);
void Gdi_FontFree(GdiFont *f);
/* GetTextExtentPoint32A: the width of n characters, and the cell's height in *cy */
int Gdi_Extent(const GdiFont *f, const char *s, int n, int *cy);
/* GetTextExtentExPointA: how many of the n characters fit in max pixels (*fit), and the whole string's
   extent (*cx, *cy) */
void Gdi_ExtentEx(const GdiFont *f, const char *s, int n, int max, int *fit, int *cx, int *cy);
/* TextOutA, TA_TOP | TA_LEFT, the background transparent (SetBkMode 1): the cell's top left at (x, y) on s
   (absolute, clipped to the surface's buffer) */
void Gdi_TextOut(Surface *s, const GdiFont *f, int x, int y, const char *str, int n, uint8_t colour);
/* Rectangle with a 1-pixel pen and a solid brush: the outline on x0..x1-1, y0..y1-1, the inside filled */
void Gdi_Rectangle(Surface *s, int x0, int y0, int x1, int y1, uint8_t pen, uint8_t brush);
/* the colour a PALETTERGB or a solid brush gets on a DIB section: the nearest of its colour table (the
   first of equals, by the squared distance) */
uint8_t Gdi_Nearest(const uint8_t pal[768], int r, int g, int b);
