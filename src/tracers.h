/* The tracers (0x46c0e0-0x46ce90): a ring of 100 records (0x4dc9d0), each following one shot with its last
   two places, drawn as the streak between them (docs/re/weapons.md, "The tracers"). */
#pragma once
#include "objects.h"
#include "render/view.h"

/* Tracers_Init 0x46c0e0: the kinds' textures and n records in a ring */
void Tracers_Init(int n);
/* Tracers_Free 0x46c1e0 */
void Tracers_Free(void);
/* Tracer_Update 0x46c220: shot s's tracer of kind k (0 and 1 the textured streaks, 2 a line) moved to its
   place, or a new one (0x46c410) taken from the ring */
void Tracer_Update(Node *s, int k);
/* Tracers_Draw 0x46c9b0: the main view's; a tracer not moved since the last one is dropped */
void Tracers_Draw(const View *view);
/* Tracers_DrawOther 0x46c5e0: the other views' (no end cap, nothing dropped) */
void Tracers_DrawOther(const View *view);
