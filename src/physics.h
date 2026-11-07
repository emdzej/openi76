/* A vehicle's simulation (0x46fe20-0x4761b0, docs/re/vehicles.md "The start"). Only the set-up at the
   start is ported so far. */
#pragma once
#include "scene.h"

/* Vehicle_PhysInit 0x471df0: a class 1 vehicle on the ground with its wheels; the wheelbase and the
   track; the body's corners and its box; its specials; the terrain class under it; the mass; the brakes;
   the engine's first step */
void Vehicle_PhysInit(Node *n);
