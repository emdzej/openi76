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
/* AI_FindWay 0x416ef0: a way past who (the controls +0xe0 / +0xe4 and *factor): going straight, steering
   ever wider either way at an eased speed; else first the throttles (unless who is a slow vehicle or going
   its way: an eased speed), then the steering by 0.25 steps to each side (first the side it is turning to
   while the AI block's +0x9d34 is ahead, else away); none clear: full lock the other way. The original
   leaves its look ahead's slow and factor unset: a look ahead that does not write them (the terrain) leaves
   what the stack held, slow0 and factor0 here (its callers pass what lies there, AI_ProbeWord) */
void AI_FindWay(Node *n, uint8_t *ai, Node *who, float *factor, int cars_only, int all_round, float slow0,
                float factor0);
/* AI_ProbeWord: word k of the last look ahead's step frame (the rotation's), still on the stack below its
   caller */
float AI_ProbeWord(int k);
/* AI_Blocked 0x415690: something right ahead: the terrain forward (2-7.5 m, its *t into +0x9d28) or any
   other entity met within the frame by 7 x the frame rate along the z axis (sphere, then a close sweep) */
int AI_Blocked(Node *n);
/* AI_TerrainAhead: 0x415080 forward (rough) from p along n's z axis: a step down of 3 m or up of 3.4 within
   2-7.5 m, or the terrain's flag */
int AI_TerrainAhead(Node *n, const float p[3], float *slow, float *t);
/* AI_CanSee 0x413570: 1 when b's centre is in sight from a's, both 1.5 m up (a's eye from its frame): not
   cut by the ground (Terrain_Line) nor by any static entity but b, each swept as the look ahead's objects
   over the frame (the line as a segment moving through it, a sphere 3 m wider, then closely) */
int AI_CanSee(Node *a, Node *b);
