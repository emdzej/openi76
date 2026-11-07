/* The sky (0x407470-0x4083f5; docs/re/render.md, "The sky"): drawn straight onto the surface before the
   render queue's flush, through a view of range 600 at the camera. */
#pragma once
#include "math3d.h"
#include "render/surface.h"
#include "render/view.h"

extern const Texture *g_sky_tex;   /* 0x4ed9d0: the world's sky map (Tex_Pixels) */
extern int g_sky_sun;              /* 0x4ed9d4: the sun or moon is drawn */
extern Surface g_sky_sun_surf;     /* 0x4edbf8: its map as a surface */
extern Frame g_sky_sun_frame;      /* 0x4ed9e0: its direction, the light's elevation about its axis */
extern double g_sky_scroll[2];     /* 0x4edc28, 0x4ed9d8: the clouds' drift (u, v) */

/* Sky_Init 0x407a10 (once until Sky_Free 0x407b10): with DET_SKY, the sky map and the sun */
void Sky_Init(void);
void Sky_Free(void);
/* Sky_Draw 0x407b40: mode 0 the flat sky only, 1 the flat sky and the ground's haze, 2 (with a sky map) the
   clouds, the haze and the sun; without a sky map always 1. colour is the flat sky's and the haze's. */
void Sky_Draw(View *view, int colour, int mode);
/* 0x4078b0: a screen-space polygon (6 floats a vertex) clipped to a * x + b * y + c > 0; a new vertex gets
   w = 0 and q */
int Sky_ClipLine(float (*out)[6], const float (*in)[6], float a, float b, float c, float q, int n);
