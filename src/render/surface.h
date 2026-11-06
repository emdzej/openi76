/* The 8-bit drawing surface and the colour tables the rasterizer reads (docs/re/render.md).

   The original's surface record (the fields the span code reads): +0 pitch (bytes per row), +0x10 x origin,
   +0x18 the pixels, +0x1c y origin; a row y, column x is pixels[(y + y0) * pitch + x0 + x]. The tables
   are the world's .lum (shade x colour, at 0x5c0450 in the original) and .tbl (translucency). */
#pragma once
#include <stdint.h>

typedef struct {
    int pitch;
    int x0;          /* +0x10 */
    uint8_t *pixels; /* +0x18 */
    int y0;          /* +0x1c */
    int w, h;        /* the port's: the buffer's size, for clipping checks in debug builds */
} Surface;

/* A screen-space vertex as the polygon fillers take them (24 bytes in the original): x, y in pixels, w = 1/z
   (+8), the texture coordinates u, v (+0xc, +0x10; times w for the perspective fillers), and q (+0x14), the
   shade term. */
typedef struct {
    float x, y;
    float w;         /* +8 */
    float u, v;      /* +0xc, +0x10 */
    float q;         /* +0x14 */
} ScreenVert;

/* A texture as the textured fillers take it: u32 width, u32 height, then the 8-bit pixels row by row. */
typedef struct {
    int32_t w, h;
    uint8_t pixels[];
} Texture;

extern uint8_t g_lum[256 * 256];   /* 0x5c0450: [shade << 8 | colour] */
