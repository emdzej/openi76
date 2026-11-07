/* The terrain's renderer (0x48e8d0-0x491180; docs/re/terrain.md, "The renderer"). */
#pragma once
#include <stdint.h>
#include "render/view.h"

/* an edge of the view's ground footprint (0x5e3100, 24 bytes): a plane facing the footprint's inside */
typedef struct {
    float a, b, c;   /* the normal */
    uint32_t junk;   /* +0xc: the original's stack word (the high dword of an edge vector's y) */
    double d;        /* +0x10 */
} TerrEdge;

extern int32_t g_terr_x0, g_terr_z0;   /* 0x5e30dc, 0x5e30e0: the footprint's first cell, aligned to 32, less 32 */
extern int32_t g_terr_depth;           /* 0x5e30e4: the bit length of its larger extent */
extern TerrEdge g_terr_edge[6];        /* 0x5e3100 */
extern int32_t g_terr_nedges;          /* 0x5e30ec */

/* Terrain_Visible 0x48eaa0: the ground footprint of the view (the eye and its four far corners, in cells),
   its bounds and its convex hull's edges. 0 if the eye is less than 0.01 above the ground or the hull has
   fewer than 3 points. Queries the terrain's height first, so it leaves the FPU rounding down. */
int Terrain_Visible(const View *v);
/* 0x48e450: the plane through p, q, r (doubles), as an edge */
void Terr_Plane(TerrEdge *out, const double p[3], const double q[3], const double r[3]);
/* 0x48e3f0: the hull's key from a to b: their angle about y (0-2pi) times 1e7, plus their distance squared */
double Terr_Key(const double a[3], const double b[3]);
