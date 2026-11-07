/* The 3D drawing entry point and its point and line cases (docs/re/render.md). */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "render/view.h"

/* What a polygon is filled with: a colour index, or for the textured modes a texture (the original passes
   one dword). */
typedef union {
    uint32_t colour;
    const Texture *tex;
} PolyArg;

extern int g_screen_h;   /* 0x58d860: the screen's height (points and lines are bigger above 480) */

/* Builds DrawPoly's mode tables from the exe's (the clip table at 0x4d7488, the fill table at 0x4d6c88).
   False if the exe is not loaded or names a routine the port lacks. */
bool Draw_Init(char *err, size_t cap);

/* DrawPoly 0x43d730: a polygon of n camera-space vertices (screen-space ones for mode bit 3), clipped,
   projected and filled per mode (9 bits). One vertex is a point, two a line. The clippers may modify the
   vertices (see clip.h). */
void DrawPoly(const View *view, void *verts, int n, PolyArg arg, int mode);
/* the clip table's routine for a mode (0x4d7488[mode]): in's n vertices clipped and projected into out
   (which may be in); the vertices left */
int Draw_Clip(const View *view, void *out, void *in, int n, int mode);
/* DrawPoint 0x43d390: a 2 x 2 dot, drawn only when the screen is taller than 480 rows. */
void DrawPoint(const View *view, const CamVert *v, uint8_t colour);
/* DrawLine 0x43d4c0: a, b clipped to the near and far planes in place (x, y, z only), projected and drawn;
   a second line beside it on screens taller than 480 rows. */
void DrawLine(const View *view, CamVert *a, CamVert *b, uint8_t colour);
/* Poly_Outline 0x43e940 (fill entry 0x43d370): the polygon's edges as lines. */
void Poly_Outline(const Surface *s, const ScreenVert *v, int n, uint8_t colour);
