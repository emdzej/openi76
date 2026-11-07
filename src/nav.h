/* The AI's navigation (0x40fda0-0x413540; docs/re/ai.md "The path search"): an incremental A* over a
   grid of 10 m cells from a vehicle to its path's next point, the cells' path then cut down and laid
   along the roads, and the bridges' links (0x4100a0). In the current rounding mode (render/fpu.h).

   The search's nodes come from a pool of 1000 (0x58dcf0, 0x80 bytes each) in the original's layout; the
   port keeps the pool as bytes and its pointers as the original's addresses, so the AI block's trees
   (+0x9cf0 the open nodes by cost, +0x9cf4 all of them by cell) hold the same words. */
#pragma once
#include "scene.h"
#include <stdint.h>

/* Nav_Init 0x40fda0: the pool's free list through all 1000 nodes */
void Nav_Init(void);
/* Nav_Search 0x412db0: up to 100 more of ai's search toward its path's point (+0x88's points, +0xa0 the
   index; from the vehicle's cell the first time). 1 while it goes on; 0 once done (the points written at
   +0xac / +0xb0, the pool freed and the trees cleared) or when the start and the point are on one road
   (0x412a00). *count gets the nodes expanded. */
int Nav_Search(uint8_t *ai, int32_t *count);
/* Nav_Reset 0x40fdf0: the pool freed and ai's search dropped (+0x7c, the trees) */
void Nav_Reset(uint8_t *ai);
/* Nav_Bridges 0x4109b0: the bridges' links rebuilt from the class 0xc objects (0x4100a0) */
void Nav_Bridges(void);
/* Road_Next 0x41ac40: the next point along the strip going dir (1 or -1), at least 1 mm from this one
   (the original's fatal after ten tries, here a log); 0 off the strip's end (*pp and *pi then moved as
   far as they went) */
struct RoadPiece;
int Road_Next(const struct RoadPiece **pp, int dir, int32_t *pi);
