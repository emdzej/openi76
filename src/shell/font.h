/* The shell's fonts (database entries 0x42, 0x44-0x47; docs/formats/mw2.md) and its text (i76shell
   0x1003c040, the aw library's 0x100315d9-0x10031797, the display's 0x1003e200). A font: its height at
   +8, then at +0x10 + 4 c the offset (from the font) of character c's glyph: its width, then width x height
   bytes, row by row. Text is drawn through a colour map whose 0xff is see-through. */
#pragma once
#include "shell/gfx.h"
#include <stdint.h>

typedef struct {
    const uint8_t *data;   /* +0, +0x408 */
    int height;            /* +0x40c */
} ShFont;

/* i76shell 0x1003c040 */
void Font_Init(ShFont *f, const uint8_t *data);
int Font_Height(const ShFont *f);                 /* aw 0x100315d9 */
int Font_Width(const ShFont *f, uint8_t c);       /* aw 0x100315ec */
int Text_Width(const ShFont *f, const char *s);
/* aw 0x1003160c: character c at x, y of the view's origin through map (NULL copies every byte); its width
   (-1 a bad bitmap, -2 an empty window) */
int Gfx_Char(const GfxView *v, int x, int y, const ShFont *f, uint8_t c, const uint8_t *map);
/* i76shell 0x1003e200: s on the screen at x, y through map (NULL the default: 0 see-through), the
   rectangle marked drawn; its width */
int Scr_Text(int x, int y, const ShFont *f, const char *s, const uint8_t *map);
