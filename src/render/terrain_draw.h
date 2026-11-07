/* The terrain's renderer (0x48e8d0-0x491180; docs/re/terrain.md, "The renderer"). */
#pragma once
#include <stdint.h>
#include "render/draw.h"
#include "render/surface.h"
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

/* A quadtree node (0x30 bytes in the original, from the pool at 0x58dcb4): a square of 2^level cells at
   (x, z), its four children (0: x, z; 1: x + h, z; 2: x, z + h; 3: x + h, z + h, each NULL if culled), and
   its vertices: v[0], v[2], v[4], v[6] the corners (x, z), (x + s, z), (x + s, z + s), (x, z + s); v[1],
   v[3], v[5], v[7] the edges' midpoints when a finer neighbour put one there (-1 none). count is the
   vertices it has (4 + midpoints); flags the last midpoint's slot. */
typedef struct TNode {
    struct TNode *next;       /* +0: the leaves' list */
    struct TNode *child[4];   /* +4 */
    int16_t split;            /* +0x14 */
    int16_t x, z, level;      /* +0x16 */
    int16_t count;            /* +0x1c */
    int16_t flags;            /* +0x1e */
    int16_t v[8];             /* +0x20 */
} TNode;

enum { TERR_NODES = 0x40000 / 0x30, TERR_VERTS = 0x1a5e0 / 12 };
extern TNode *g_terr_leaves;              /* 0x5824a4: the leaves, in order */
extern float (*g_terr_vert)[3];           /* 0x58dcb0: the vertices (cells, raw height; camera space once transformed) */
extern int32_t g_terr_nverts;             /* 0x5e30e8 */
extern float g_terr_split;                /* 0x5e30c8: |sx| / (pitch / 2) * lod */
extern float g_terr_eye[3];               /* 0x5e30f4, 0x5e30f8, 0x5e30fc: the eye in cells (y * 0.625) */
extern int32_t g_terr_eye_x, g_terr_eye_z; /* 0x5e30d4, 0x5e30d8 */
extern float g_terr_low;                  /* 0x5e30f0: the lowest vertex's height (m) */

/* Terrain_Draw 0x48e8d0: the view's terrain into the render queue (or, with the eye under the ground, the
   surface filled with colour 0xb4) */
void Terrain_Draw(const View *v);
/* Surface_Fill 0x439390 */
void Surface_Fill(Surface *s, uint8_t c);
/* the patch drawer Terrain_Draw ends with (0x48f7b0, or 0x48ffa0 with the setting DET_TERRTEX); tests
   replace it */
extern void (*g_terr_patches)(void);

/* a texture level (0x5821c8, 0x68 bytes, seven of them): the texture coordinates of the patch's eight
   vertex slots (u, v and an unused third), its texture (+0x60) and DrawPoly mode (+0x64) */
typedef struct {
    float uv[8][3];
    PolyArg tex;
    int32_t mode;
} TerrLevel;
extern TerrLevel g_terr_level[7];
extern float g_terr_sun[3];   /* 0x5824b0: the sun's direction in camera space */
extern float g_terr_ambient;  /* 0x5824a0: Light_Strength2 */

/* Terrain_LoadTextures 0x48e550, Terrain_FreeTextures 0x48e650, Terrain_ReloadTextures 0x48e690 */
void Terrain_LoadTextures(void);
void Terrain_FreeTextures(void);
void Terrain_ReloadTextures(void);
/* Terrain_Shade 0x48ff10: a polygon's light: the sun on its normal (a, b, c; camera space, none from
   behind) times a depth falloff k1 * z + k0 (g_light_k) held to 0.75-1, plus the ambient. Unrounded. */
double Terrain_Shade(float a, float b, float c, float z);
