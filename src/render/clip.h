/* Clipping and projection of polygons for the fillers (docs/re/render.md). Each takes the view, the output
   array (room for 17 vertices), the input and its count, and DrawPoly's mode; each returns the output's count
   (0: nothing to draw). */
#pragma once
#include "render/view.h"

/* Proj_Untextured 0x4412e0 / Proj_Textured 0x441340 (mode bit 8: no clipping): camera space to screen space. */
int Proj_Untextured(const View *view, ScreenVert *out, const CamVert *in, int n, int mode);
int Proj_Textured(const View *view, ScreenVert *out, const CamVert *in, int n, int mode);
/* Clip_Screen 0x440350 / Clip_ScreenTex 0x43fbf0 (mode bit 3: the input is already in screen space): clipped
   to the view's rectangle. */
int Clip_Screen(const View *view, ScreenVert *out, const ScreenVert *in, int n, int mode);
int Clip_ScreenTex(const View *view, ScreenVert *out, const ScreenVert *in, int n, int mode);
/* Clip_Camera 0x4409c0 / Clip_CameraTex 0x440d90: clipped to the near and far planes, projected, then clipped
   to the rectangle. The textured one, untiled (mode bit 1 clear), clamps the input's u, v to 0..1 in place
   (in the caller's array when the polygon needs no z clipping). */
int Clip_Camera(const View *view, ScreenVert *out, const CamVert *in, int n, int mode);
int Clip_CameraTex(const View *view, ScreenVert *out, CamVert *in, int n, int mode);
