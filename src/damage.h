/* A vehicle's state from step to step (0x42b830-0x42d800, docs/re/vehicles.md "The state"): the engine
   started and stopped, the lights, and the wreck's timer. The damage itself (0x45d7c0, 0x42a5d0) and
   the death (0x429eb0, 0x42c580) are not ported yet. */
#pragma once
#include "scene.h"

/* Node_SetFlags 0x42e2d0 and Node_ClearFlags 0x42e320: bits set (cleared) on the node, its first child
   and everything under and after that child (0x42e250, 0x42e290); NULL does nothing */
void Node_SetFlags(Node *n, uint32_t bits);
void Node_ClearFlags(Node *n, uint32_t bits);
/* Vehicle_Lights 0x42d660: toggle flips the lights (+0x454 bit 0x40); the headlamps (the node +0x3a0
   and the beam +0x440) show while on and both front lamps are at least half whole (+0x178 / +0x158,
   +0x18c / +0x168), the brake lamps (+0x3a4) while the throttle is below -0.2 and both rear lamps are
   (+0x184 / +0x164, +0x198 / +0x174); with toggle the light source too (Light_SetOn) */
void Vehicle_Lights(Node *n, int toggle);
/* Engine_StartTime 0x476510: for the user's vehicle the length of the engine's start sound (esnp1-3.wav
   by the engine's state, +0 / +4, and its kind +0x28; the table 0x4ddb40), 0 otherwise */
float Engine_StartTime(Node *n);
/* Vehicle_State 0x42b830, each sub-step: the wreck begun (0x8000 without 0x4000, 0x42c580); dead (0x20)
   the engine stops; else the lights' key (+0x100) and the engine's key (+0xec: a stopped engine starts,
   +0x450 the time, bits 0x80000008; a running one stops); then with bit 0x80000000 the timer runs down
   by dt and at 0 the engine runs (bit 1) or the wreck is removed. The flags as read at the start are
   written back with the timer, so a change made meanwhile (the lights) is lost then */
void Vehicle_State(Node *n, float dt);
