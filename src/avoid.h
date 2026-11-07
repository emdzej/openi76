/* The AI's look ahead (0x416a80 and 0x415080-0x4163d0; docs/re/ai.md "Avoiding"): the vehicle stepped
   a frame ahead with given controls (Vehicle_Probe), then what it would meet: the terrain's steps to
   either side, the other vehicles, the static objects (their spheres, then their boxes or COLP boxes),
   and at random the shots. In the current rounding mode (render/fpu.h). */
#pragma once
#include "scene.h"
#include <stdint.h>

/* AI_Avoid 0x416a80: with steering steer and throttle throttle, 1 when something is ahead: *who what
   (NULL for the terrain or a shot), *slow 1 for a body, *factor how much to slow (0.3-1.25), *t the time
   to it. cars_only: only the class 1 vehicles (no terrain, objects or shots); all_round: the vehicles
   in any direction. 0 with *slow 0 and *factor 0.5 */
int AI_Avoid(Node *n, float steer, float throttle, Node **who, float *slow, float *factor, int cars_only,
             int all_round, float *t);
