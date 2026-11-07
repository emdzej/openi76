/* The view: the camera, its projection onto a surface and its frustum, 0x1d8 bytes in the original (built
   by View_Create 0x43c8a0, moved by View_SetCamera 0x43cf00). The fields and their offsets are the original's;
   on 64-bit hosts the surface pointer fills +0x38-0x3f, which the original leaves at 0 after the pointer. */
#pragma once
#include "math3d.h"
#include "render/surface.h"

typedef struct {
    float cx, cy;                   /* +0, +4: the screen position of the view axis (the surface's middle) */
    float sx, sy;                   /* +8, +0xc: the projection's scale (x * sx / z + cx); sy < 0, y is up */
    float range;                    /* +0x10: the far distance at zoom 1 (100-1000) */
    float far_radius;               /* +0x14: the far corners' distance, range * zoom * sqrt(1 + tan^2) */
    float radius600;                /* +0x18: the same at a distance of 600 */
    float xmin, ymin, xmax, ymax;   /* +0x1c..+0x28: the 2D clip rectangle (the surface's viewport) */
    float tan_half;                 /* +0x2c: tan(field of view / 2) */
    float aspect;                   /* +0x30 */
    float zoom;                     /* +0x34: 0.5-8 */
    Surface *surface;               /* +0x38: what the fillers draw on */
    Frame cam;                      /* +0x40: world to camera space (the camera frame's inverse) */
    double centre[3];               /* +0x80: the middle of the frustum's bounding box (world) */
    double radius;                  /* +0x98: the frustum's bounding sphere round centre */
    float plane[6][4];              /* +0xa0: n.x, n.y, n.z, d (camera space), facing in: the sides through the
                                       screen's left, right, top and bottom edges (+0xa0, +0xb0, +0xc0,
                                       +0xd0), the far plane (+0xe0) and the near (+0xf0) */
    float near_c[4][3];             /* +0x100: the near rectangle's corners (camera space; z = 0.1, znear) */
    float far_c[4][3];              /* +0x130: the far rectangle's (z = range * zoom up to 1500, zfar) */
    double eye[3];                  /* +0x160: the camera's position (world) */
    double corner[4][3];            /* +0x178: far_c in world space */
} View;

#define VIEW_ZNEAR(v) ((v)->near_c[0][2])   /* +0x108 */
#define VIEW_ZFAR(v) ((v)->far_c[0][2])     /* +0x138 */

/* A camera-space vertex: x, y, z, the texture's u, v (0..1) and q, the light term. */
typedef struct {
    float x, y, z;
    float u, v;
    float q;
} CamVert;

/* View_Create 0x43c8a0: a view of the surface's viewport, with a field of view (radians, pi/4-3pi/4), an
   aspect, a range (100-1000) and a zoom; the camera at (0, 1, 0) looking down +z. */
void View_Create(View *view, Surface *surface, float fov, float aspect, float range, float zoom);
/* View_SetZoom 0x43ca60: the zoom (0.5-8), then View_SetRect with the view's rectangle. */
void View_SetZoom(View *view, float zoom);
/* View_SetRect 0x43cb40: the surface's viewport (clipped to the surface), then the frustum. */
void View_SetRect(View *view, int x0, int y0, int x1, int y1);
/* View_SetCamera 0x43cf00: puts the camera at a frame (world). */
void View_SetCamera(View *view, const Frame *frame);
/* View_ToCamera 0x43d2d0: a world point in camera space. */
void View_ToCamera(const View *view, float out[3], double x, double y, double z);
/* View_TestSphere 0x43d180: a camera-space sphere against the frustum: 1 wholly outside, 0 wholly inside,
   negative when it crosses a plane. */
int View_TestSphere(const View *view, const float c[3], float r);

/* Surface_SetViewport 0x4392f0: the viewport, its corners in either order, clipped to the surface; unchanged
   when wholly off it */
void Surface_SetViewport(Surface *s, int x0, int y0, int x1, int y1);
/* Rect_Clip 0x43fb00: r (x0, y0, x1, y1) clipped to c: -1 when wholly outside, 0 inside, 1 clipped */
int Rect_Clip(int r[4], const int c[4]);
