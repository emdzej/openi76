/* The game's bitmap font (base6x7*.fnt) and its text regions: boxes of lines drawn over the frame
   (docs/re/map.md "The scores' box") */
#pragma once
#include "render/surface.h"
#include <stdint.h>

typedef struct BFont BFont;
/* BFont_Load 0x4394e0: count, height and the glyphs (a width, then width x height bytes, made masks:
   anything but 0xff drawn); its colour 0xf */
BFont *BFont_Load(const char *name);
void BFont_Free(BFont *f);                     /* 0x4396c0 */
void BFont_Colour(BFont *f, int c);            /* 0x4396d0: c in 0-0xff, else none (the pixels kept) */
/* 0x439900: str at (x, y) scaled from 640 x 480 to the screen, tabs at the tab stops */
void BFont_Draw(BFont *f, Surface *s, int x, int y, const char *str);
int BFont_Height(const BFont *f);

/* the screen mode's font (0x58dcc0, WinMain's: base6x76.fnt at 640 x 480) */
extern BFont *g_game_font;

typedef struct Region Region;
/* Regions_Init 0x44fca0: the list emptied, the line metrics from base6x76.fnt ('W''s width, the height) */
void Regions_Init(void);
/* Region_New 0x44fd00: a box at (x, y), w x h, its lines in colour (0xbbggrr), flags 8 (fixed lines, no
   scrolling), 0x10 (one line high), 1 (lines last life s) */
Region *Region_New(int x, int y, int w, int h, uint32_t colour, float life, uint32_t flags);
void Region_Tabs(Region *r, int n, const int32_t *tabs);   /* 0x44fea0 */
void Region_Show(Region *r, int on);                        /* 0x44fed0 */
void Region_Clear(Region *r);                               /* 0x44ff10 */
void Region_Add(Region *r, const char *line);               /* 0x44ff70 */
void Regions_Draw(Surface *screen);                         /* 0x450040 */
void Regions_Free(void);                                    /* 0x4503a0 */
