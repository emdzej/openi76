/* The roads (RDEF, 0x4909e0-0x492f20; docs/re/roads.md): a mission's road strips, cut into pieces of at
   most 25 points, each point a pair (left x, y, z, right x, y, z) set on the ground at the mission's
   start, and their 30 textures. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "render/surface.h"

/* a piece (0x2c bytes, then its points) */
typedef struct RoadPiece {
    int32_t id;                  /* +0: the RSEG's number (0x4eb6fc) */
    struct RoadPiece *next;      /* +4 */
    struct RoadPiece *prev;      /* +8 */
    int32_t last;                /* +0xc: the RSEG's last piece */
    int32_t type;                /* +0x10: the RSEG's +8 (its textures' row, type % 3) */
    int32_t n;                   /* +0x14: points */
    float c[3], r;               /* +0x18: the bounding sphere (Roads_Setup) */
    float (*pts)[6];             /* +0x28 */
    float data[][6];             /* +0x2c */
} RoadPiece;

extern RoadPiece *g_roads;              /* 0x4eb6f8 (Roads_First 0x492f20) */
extern int g_roads_ready;               /* 0x4eb3a0 */
extern char g_road_names[30][16];       /* 0x58a528 */
extern const Texture *g_road_tex[30];   /* 0x58a708 */

/* the RDEF chunk's handler 0x484600 / 0x492cf0: RREV, RSEG (repeated), ISEG, TSEG, EXIT (0x4eb618) */
int Roads_Rdef(const uint8_t *payload, void *name, const uint8_t *end);
/* RSEG 0x492d20 (the chunk, header included) */
int Roads_Seg(const uint8_t *c, void *arg, const uint8_t *end);
/* Roads_Setup 0x4909e0: the texture names (Roads_Names), the textures (Res_Get; a missing one is the
   original's fatal message, here a log), then every piece on the ground (Roads_Ground) */
void Roads_Setup(const char *mission);
/* 0x490de0: the 30 default names (0x4eb3b8), then those of <mission's name>.rtm, a name a line */
void Roads_Names(const char *mission);
/* the rest of 0x4909e0: each point's y the terrain's height there, the pieces' spheres; then ready */
void Roads_Ground(void);
/* the pieces (the original never frees them) */
void Roads_Free(void);
/* for tests */
const void *Roads_Table(void);
/* Roads_Nearest 0x490f80: the road point nearest (x, z) by its right side, among the pieces whose sphere's
   centre is nearest: each piece in turn joins at most eight kept in order, but only one no farther than the
   farthest kept (a quirk: a farther one is dropped even with room). *piece is NULL without roads (the
   index is then left alone). */
void Roads_Nearest(float x, float z, RoadPiece **piece, int32_t *index);
