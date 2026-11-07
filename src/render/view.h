/* The view record the 3D drawing entry points take (DrawPoly 0x43d730 and its clippers): the fields known so
   far, at their offsets in the original. */
#pragma once
#include "render/surface.h"

typedef struct {
    float cx, cy;                   /* +0, +4: the screen position of the view axis */
    float sx, sy;                   /* +8, +0xc: the projection's scale (x * sx / z + cx) */
    float xmin, ymin, xmax, ymax;   /* +0x1c..+0x28: the 2D clip rectangle (floats) */
    Surface *surface;               /* +0x38: what the fillers draw on */
    float znear;                    /* +0x108 */
    float zfar;                     /* +0x138 */
} View;

/* A camera-space vertex: x, y, z, the texture's u, v (0..1) and q, the light term. */
typedef struct {
    float x, y, z;
    float u, v;
    float q;
} CamVert;
