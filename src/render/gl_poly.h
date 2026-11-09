/* The port's GL polygon backend (openi76-gl.wasm only, `--param glpoly=1`): the second step of the added GL
   renderer (docs/re/port-plan.md, "What the port adds"). Not the original's.

   The ported pipeline still fills every polygon in software, so the 8-bit frame stays complete and in the
   original's order. The polygons this backend supports are drawn by GL as well, into an index framebuffer of
   the frame's size, and an ownership plane records which pixels such a polygon wrote last, with the value.
   At presentation a pixel shows GL's index where it owns it and the frame still holds that value (nothing
   drawn over it since), else the software pixel: sprites, 2D, the translucent and the other modes keep the
   ported fillers' look. At the frame's own size the picture is the software one but for the rasterizers'
   edges and rounding; step 4 (resolution) is what makes it differ.

   The interface is screen-space triangle fans and a material, not the fillers' vertex records, so a later
   camera-space producer can feed it too. */
#ifndef OPENI76_GL_POLY_H
#define OPENI76_GL_POLY_H
#include <stdbool.h>
#include <stdint.h>
#include "render/surface.h"

/* A vertex: x, y in the frame's pixels (a pixel's centre at integers, as the fillers sample), w = 1/z (used
   by perspective materials), u, v in texels (the texel is floor(u), floor(v), clamped to the texture or, tiled,
   masked by its size - 1), and the shade, a row of the lum table (floor(shade) & 255; interpolated linearly
   in screen space, as the fillers do). */
typedef struct {
    float x, y, w, u, v, shade;
} GlVert;

enum {
    GL_MAT_SOLID,     /* the colour index, as it is */
    GL_MAT_SHADED,    /* lum[shade row][colour] */
    GL_MAT_TEXTURE,   /* the texel, as it is */
    GL_MAT_TEXTURE_SHADED,   /* lum[shade row][texel] */
};

typedef struct {
    int kind;
    uint8_t colour;
    const Texture *tex;   /* the textured kinds' */
    bool persp;           /* u, v interpolated in perspective (by w); else in screen space */
    bool tiled;           /* texel indices masked by the size - 1 (power-of-two textures) */
} GlMaterial;

/* true when the backend is on and pixels is the frame it draws for (the last one presented) */
bool gl_poly_on(const uint8_t *pixels);
/* records a convex polygon of n vertices; false if it cannot be drawn (its pixels then stay software's) */
bool gl_poly_draw(const GlVert *v, int n, const GlMaterial *m);
/* around a recorded polygon's software fill: the frame's rectangle x0..x1, y0..y1 (inclusive) before, then
   the pixels the fill changed become the polygon's */
void gl_poly_cover_begin(int x0, int y0, int x1, int y1);
void gl_poly_cover_end(void);

/* At presentation (gl_present.c): for a frame of pixels, draws the recorded polygons and uploads the
   ownership plane; true with the two textures (RG8UI: index and coverage; value and owned) if this frame
   has them. Then starts the next frame. */
bool gl_poly_compose(const uint8_t *pixels, int w, int h, int pitch, unsigned *index_tex, unsigned *own_tex);
/* a frame that is not shown: what was recorded is dropped */
void gl_poly_discard(void);

#endif
