/* The AI's road war (0x41aec0-0x41c950; docs/re/ai.md "The road war"): two vehicles on one strip of road
   (the RSEG pieces joined from one with +0xc clear to the next one with it set), each with its place in
   its AI block: +0xa950 the road point, +0xa954 the way along (1 or -1), +0xa958 the lane's band (0-5),
   +0xa95c how it faces the other (0-3), +0xa964 its signed distance off the road's line, +0xa968 a
   heading and +0xa96c the piece. In the current rounding mode (render/fpu.h). */
#pragma once
#include "scene.h"
#include <stdint.h>

/* Road_Place 0x41b7f0: the place's distance off its point's segment (+0xa964) and band (+0xa958), the
   distance made negative on the left (by +0xa954); band 2 and 3.45 m at the strip's end */
void Road_Place(float x, float y, float z, uint8_t *ai);
/* Road_Aspect 0x41aec0: how n faces: with band 0 (on one point) which quarter other lies in from its
   heading (0 under 315 degrees on, else the quarter), else by the road ahead: 0 along it, 2 against or
   at the end, 1 or 3 across */
int Road_Aspect(Node *n, uint8_t *ai, int band, Node *other);
/* Road_Steer 0x41b2b0: the heading (*out) to the point lead metres on along the road from (x, y, z), the
   road's line moved offset metres to the side (times the way); 1 when it lies on the road, 0 past the
   strip's end (the heading then to the last point, or n's own off the end at once) */
int Road_Steer(Node *n, float x, float y, float z, uint8_t *ai, float lead, float offset, float *out);
/* Road_Pace 0x41b6e0: keeping up with t: 4 m past its distance off the line (-10 beyond -12 m, 20
   beyond 22), at t's speed plus 5 a point between them (cruising when steering hard) */
void Road_Pace(Node *n, float x, float y, float z, uint8_t *ai, uint8_t *tai, float lead, Node *t);
/* Road_War 0x41ba90: one frame of n's fight with t on the road by the table at 0x4d25d0 (mode, which
   side of t it drives, how far apart along the road, how each faces the other) */
void Road_War(Node *n, Node *t, int mode);
/* Road_Chase 0x41c6b0 (tactic 0xa's frame): driving down t's road at its side, turning back at the
   strip's ends. left is the offset the original reads from its unset local on the paths that never set
   it (a word of the caller's stack; docs/re/ai.md). */
void Road_Chase(Node *n, Node *t, float left);
/* 0x41c250: n and t (a vehicle on the ground, 0x419fd0) are on one road: their nearest points (both from
   n's place, a quirk) on pieces of one RSEG; never for class 9 */
int Road_Together(Node *n, Node *t);
/* 0x419fd0: t is not a vehicle, or is off the ground's classes 2-4 (+0x45c) */
int Road_Off(Node *t);
