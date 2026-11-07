/* The palette module (0x43b5a0-0x43c250): the palette as floats, the nearest colour, and the colour
   tables of g_rtab that depend on the palette (docs/re/render.md, "Tables"). */
#pragma once
#include <stdint.h>

extern float g_pal_rgb[256][3];   /* 0x5ae550: each channel / 255 */

/* Palette_Set 0x43bcc0: the palette (768 bytes, RGB) as floats; lum (64 KB, a world's .lum) is copied
   to the lum table. Without one the original builds a lum table from the palette (not yet ported:
   the port leaves the table as it is). */
void Palette_Set(const uint8_t pal[768], const uint8_t *lum);
/* Color_Match 0x43c050: the nearest colour to (r, g, b) in 0..1 (the first of equals) */
int Color_Match(float r, float g, float b);
/* Color_Get 0x43c010 */
void Color_Get(int c, float *r, float *g, float *b);
/* Remap_Reset 0x43b5a0: the remap table's rows each all their own colour (opaque), the identity row
   (+0x11000) and the row of 0xff (+0x11100) */
void Remap_Reset(void);
/* Remap_Set 0x43c0c0: texel t drawn as colour c */
void Remap_Set(int t, uint8_t c);
/* Remap_Key 0x43c100: colour c see-through: its remap and tbl rows become the identity, and the
   +0x11100 row all c */
void Remap_Key(uint8_t c);
/* Lum_Ramp 0x43b5f0 (its first argument unused): colour c's lum entries, shades 0-31 from colour
   `from` to `to` (as numbers, in 8.16 steps), shades 32-143 as shade 31, shades 144-255 as shade 0 */
void Lum_Ramp(int from, int to, uint8_t c);
/* Glow_Build 0x43b690: from the palette (in HSV), the glow table's 16 rows of each colour, its value
   stepped up towards 0.95 (rows past 15 run into the tbl table), the shadow row at +0x11200 (0.6 below
   the last step), and colours 0xe0-0xef glowing as themselves. Needs Palette_Set's floats. */
void Glow_Build(const uint8_t pal[768]);

/* Gamma_Build 0x43c1e0: ten rows (0x5adb50), one per gamma 0.8, 0.9, 1.0, 1.2, 1.4, 1.6, 1.8, 2.0, 2.2,
   2.5: entry i is (i / 255)^(1/gamma) * 255, truncated. */
extern uint8_t g_gamma[10][256];
void Gamma_Build(void);
/* Gamma_Apply 0x43c170: colours first..end-1 of pal through gamma row n (1-10; below 1 the setting g_detail.b[DET_GAMMA], 0x5e14dc; outside 1-10, 3: gamma 1.0) */
void Gamma_Apply(uint8_t *pal, int first, int end, int n);
/* The palette path of the display's set-palette 0x437b40: colours first..first+count-1 of pal into the
   shown palette (the display record's +0xe0), colour 0 forced black, gamma applied to colours
   first..count-1 (the original passes the count as the end), then Palette_Set on the result: the
   float palette and the colour matching see the gamma-corrected colours. */
void Display_SetPalette(uint8_t shown[768], int first, int count, const uint8_t *pal, const uint8_t *lum);
