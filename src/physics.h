/* A vehicle's simulation (0x46fe20-0x4761b0, docs/re/vehicles.md "The start"). Only the set-up at the
   start is ported so far. */
#pragma once
#include "scene.h"

/* Vehicle_PhysInit 0x471df0: a class 1 vehicle on the ground with its wheels; the wheelbase and the
   track; the body's corners and its box; its specials; the terrain class under it; the mass; the brakes;
   the engine's first step */
void Vehicle_PhysInit(Node *n);

/* Vehicle_Kill 0x42b790: a class 1 vehicle not already dying (+0x454 bits 0x8020) dies (bit 0x20, 0x429eb0) */
void Vehicle_Kill(Node *n);
/* Vehicle_Upright 0x46fe20: a vehicle turning slowly with its up axis along the ground's normal
   (+0x460) but reversed: a stuck object stops turning (+0xc8), the user's (or Taurus's, +0x454 bit
   0x10000) is turned back over, any other dies. 1 when it is not resting on its back or side */
int Vehicle_Upright(Node *n);
