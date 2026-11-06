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

/* A screen-space vertex as the polygon fillers take them (24 bytes in the original): x, y in pixels, then
   four more floats; +0x14 (q) is the depth term the flat filler shades with. */
typedef struct {
    float x, y;
    float u, v;      /* to be confirmed by the textured fillers */
    float w;
    float q;         /* +0x14 */
} ScreenVert;

extern uint8_t g_lum[256 * 256];   /* 0x5c0450: [shade << 8 | colour] */
