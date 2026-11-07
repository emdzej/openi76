/* The AI's orders and tactics (0x40cc10 and the tactics table 0x4a9400; docs/re/ai.md "The orders and the
   tactics"). The table's functions are read from the exe and dispatched by address to their ports; one not
   ported yet does nothing (and answers 0). */
#pragma once
#include "scene.h"

/* AI_Order 0x40cc10: vehicle n given an order (0-0x21) on target t (may be NULL); slow is 0x5054ac */
void AI_Order(Node *n, Node *t, int order, int slow);
/* AI_Stop 0x40a500: the throttle to the most braking without a slide (0x474eb0 at -1, x 0.9999) */
void AI_Stop(Node *n);
/* Vehicle_Throttle 0x474eb0: the throttle nearest want (from its half, six halvings) whose force does not
   slide; want itself far from the camera (View_Distance 25: 2) or for class 9 */
float Vehicle_Throttle(Node *n, float want);
/* Vehicle_SetMode 0x42d500: a class 1 vehicle's mode (+0x104, the gear) */
void Vehicle_SetMode(Node *n, int32_t mode);
/* Engine_Stop 0x42d560 and Engine_Start 0x42d5a0 (startCar): a running engine stops (the sound 2); a
   stopped one not starting begins to (+0x450 its start time, bits 0x80000008) */
void Engine_Stop(Node *n);
void Engine_Start(Node *n);
/* 0x409bc0: an order touching the user's vehicle (0x505498) sets 0x505494 (the user's controls held) */
void AI_TouchUser(Node *n);
extern Node *g_ai_user;   /* 0x505498 (0x409b20) */
