/* The AI's driving controls (docs/re/ai.md "Driving a path"): following the path the search left in the AI
   block (+0xa8 the point driven to, +0xac the count, +0xb0 the points), steering toward an angle and holding
   a speed. In the current rounding mode (render/fpu.h). */
#pragma once
#include "scene.h"
#include <stdint.h>

/* AI_Heading 0x422020 (0x42e3f0): the node's heading, atan2 of its z axis's x and z (for a vehicle times its
   direction +0xe8, so a reversing one faces back); 0 for an upright axis. As it was when first asked for
   this frame (combat.c's cache); AI_HeadingNow computes it. */
float AI_Heading(Node *n);
float AI_HeadingNow(Node *n);
/* Node_Heading 0x422200 (0x42e440): the same entry's plain heading, atan2 of the z axis's x and z, whatever
   the vehicle's direction (the compass's) */
float Node_Heading(Node *n);
/* Angle_Diff 0x4137c0: b - a folded once into [-pi, pi] */
double Angle_Diff(float a, float b);
/* Dir_Cos 0x40f2d0: the cosine between b - a and p - a (1 when either is under 0.01 long) */
double Dir_Cos(float ax, float az, float bx, float bz, float px, float pz);
/* AI_SteerLimit 0x474fb0: the steering the tyres hold at the vehicle's speed (1 far from the camera, for
   class 9, or slow) */
float AI_SteerLimit(Node *n);
/* AI_Steer 0x40a130: the steering (+0xe0) toward heading a, through the AI block's gain (+0xa820) and
   within the tyres' limit and +-0.9999; 0 when it cannot turn (the +0x454 bit 4, still, no load) */
void AI_Steer(Node *n, float a);
/* AI_Speed 0x409cc0: the throttle (+0xe4) toward speed want: the acceleration it needs (the AI block's
   +0xa81c gain) less the drag, over the engine's or the brakes' force, within what does not slide */
void AI_Speed(Node *n, float want);
/* AI_Cruise 0x409c90: the AI block's cruising speed (+0xa994); 10 for class 9 */
float AI_Cruise(Node *n);
/* Path_Advance 0x40f4d0: the point driven to moves on: past the first once within 725 (squared), past
   each middle one while the next lies ahead, the last reached within 10 m (+0x98, +0x94); then the
   script's point (+0xa0) reached within 40 m moves on, wrapping with +0x9c set */
void Path_Advance(uint8_t *ai);
/* Path_Aim 0x40f6d0: the heading to the point look metres along the path from the point driven to (24 for
   class 9), with side set each point moved 5 m to the right of its segment */
float Path_Aim(Node *n, uint8_t *ai, int side, float look);
