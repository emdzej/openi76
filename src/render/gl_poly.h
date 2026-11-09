/* The port's GL polygon backend (openi76-gl.wasm only, `--param glpoly=1`): the second step of the added GL
   renderer (docs/re/port-plan.md, "What the port adds"). Not the original's.

   The ported pipeline still fills every polygon in software, so the 8-bit frame stays complete and in the
   original's order. The polygons this backend supports are drawn by GL as well, into an index framebuffer of
   the frame's size, and an ownership plane records which pixels such a polygon wrote last, with the value.
   At presentation a pixel shows GL's index where it owns it and the frame still holds that value (nothing
   drawn over it since), else the software pixel: sprites, 2D, the translucent and the other modes keep the
   ported fillers' look. The ops that read the destination (translucency, remap, glow, shadow) read GL's
   own framebuffer, copied for each such polygon. At the frame's own size the picture is the software one but for the rasterizers'
   edges and rounding. Step 4's parameters make it differ: `--param glscale=<N|fit>` draws GL's framebuffer at
   N times the frame's size (fit: the drawable's), the ownership staying per frame pixel; `--param glfilter=1`
   filters the texels and the shade rows (the result blended in RGB through the frame's palette, the index
   kept for the ops that read the destination); `--param gldepth=1` tests a depth buffer of 1/z, so the
   queue's sorting mistakes go.

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

/* what the pixel becomes, from the material's value i (and for the last five the destination d, the
   index already there): as the textured fillers' pixel lines and the shadow filler's */
enum {
    GL_OP_COPY,      /* i */
    GL_OP_KEY,       /* i, but a texel 0xff leaves the pixel (keyed before the shade) */
    GL_OP_REMAP,     /* remap[i << 8 | d] */
    GL_OP_TBL,       /* tbl[i << 8 | d] */
    GL_OP_TBL_REV,   /* tbl[d << 8 | i], a texel 0xff leaving the pixel */
    GL_OP_GLOW,      /* glow[i << 8 | d] (its rows past 15 run into tbl, as the block's) */
    GL_OP_SHADOW,    /* the darker colour of d, each pixel once a frame */
};

typedef struct {
    int kind;
    int op;
    uint8_t colour;
    const Texture *tex;   /* the textured kinds' */
    bool persp;           /* u, v interpolated in perspective (by w); else in screen space */
    bool tiled;           /* texel indices masked by the size - 1 (power-of-two textures) */
    int rows_last;        /* the fillers' row table: rows rows_last..+3 below the height read row rows_last - 1
                             (row 0 for a negative one; poly.h's Poly_TexRows); the height for none */
} GlMaterial;

/* the view the next polygons are drawn in (a key, its rectangle on the frame, inclusive, and its near
   plane): with the depth buffer, the depth of a view's rectangle is cleared at its first polygon of a
   frame (the mirror and the cockpit draw over the world) */
void gl_poly_view(const void *key, int x0, int y0, int x1, int y1, float znear);

/* what the next polygons are, as the queue's flush draws them: with the depth buffer only the objects and
   the queue's other records write it; the ground (its records of types 0 and 1, the terrain and the roads)
   and what is drawn outside the queue (the sky, whose depths mean nothing) are tested only, so they keep
   the queue's order among themselves (the roads lie in the terrain's plane and, close up, below it) */
enum { GL_LAYER_NONE, GL_LAYER_GROUND, GL_LAYER_OBJECT };
void gl_poly_layer(int layer);

/* true when the backend is on and pixels is the frame it draws for (the last one presented) */
bool gl_poly_on(const uint8_t *pixels);
/* records a convex polygon of n vertices; false if it cannot be drawn (its pixels then stay software's) */
bool gl_poly_draw(const GlVert *v, int n, const GlMaterial *m);
/* around a recorded polygon's software fill: the frame's rectangle x0..x1, y0..y1 (inclusive) before, then
   the pixels the fill changed become the polygon's; for an op that reads the destination only where that
   was GL's too (else GL's value would be from another destination: the pixel goes back to software).
   cover_end returns how many pixels it left in doubt: unchanged, and not GL's with that value already (a
   fill can write the value a pixel had; the frame is not cleared between frames). For those the caller
   runs the fill again into the probe, a scratch frame whose rectangle is a probe value (pass 0: 0, 1:
   0xff): a pixel the fill overwrites there was written. probe_end returns how many are still in doubt
   (written with the probe's value). */
void gl_poly_cover_begin(int x0, int y0, int x1, int y1, bool reads_dest);
int gl_poly_cover_end(void);
uint8_t *gl_poly_probe(int pass);
int gl_poly_probe_end(int pass);

/* what a frame's composite reads: index (RG8UI: the index, covered) and colour (RGBA8) at w x h, own (RG8UI:
   the value, owned) at the frame's size */
typedef struct {
    unsigned index, colour, own;
    int w, h;
} GlPolyFrame;

/* At presentation (gl_present.c): for a frame of pixels, draws the recorded polygons (pal: the frame's palette,
   R G B, and pal_tex, as an RGBA8 256 x 1 texture) and uploads the ownership plane; true with the textures if this frame has them.
   dw, dh: the drawable, for glscale=fit. Then starts the next frame. */
bool gl_poly_compose(const uint8_t *pixels, int w, int h, int pitch, const uint8_t pal[768], unsigned pal_tex, int dw,
                     int dh, GlPolyFrame *out);
/* --param gldebug=1: the composite colours the pixels where GL's is not shown (gl_present.c) */
bool gl_poly_debug(void);

/* a frame that is not shown: what was recorded is dropped */
void gl_poly_discard(void);

#endif
