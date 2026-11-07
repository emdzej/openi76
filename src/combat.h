/* The AI's fighting (docs/re/ai.md "Attacking"): the attack tactic's state table (0x41e0d0) and its
   manoeuvres, the target's predicted place, and the AI block's list of its weapons (0x408c40). In the
   current rounding mode (render/fpu.h). */
#pragma once
#include "scene.h"
#include <stdint.h>

/* Node_CentreNow 0x424130: the world place of the centre of n's entity (the entity of the node at the top
   of its previous-sibling chain: its frame, or n's own in the world for a group), and [3] its radius; all
   0 without an entity. Node_Centre 0x4223e0: the same as it was when first asked for this frame (the
   relations' cache below). */
void Node_CentreNow(float out[4], Node *n);
void Node_Centre(float out[4], Node *n);
/* Rel_Clear 0x421fc0: the relations' cache emptied (each frame's end, and the mission's start) */
void Rel_Clear(void);
/* Pair_Angle 0x423220 (0x42e4a0): the heading from a's centre to b's; Pair_Dist 0x4227b0 (0x42e4c0): the
   distance between them over the ground */
float Pair_Angle(Node *a, Node *b);
float Pair_Dist(Node *a, Node *b);
/* Pair_Dist3 0x422dc0 (0x42e4e0): the distance between them in space */
float Pair_Dist3(Node *a, Node *b);
/* Angle_Sector 0x42e500: which eighth of the turn b lies from a, 0 straight ahead (centred), clockwise */
int Angle_Sector(float a, float b);
/* Class_Velocity 0x425be0: a class's velocity and speed (vehicles and helicopters: +0xbc, +0xac; the
   buildings' none) */
void Class_Velocity(Node *n, float v[3], float *speed);
/* Combat 0x41e0d0: one frame of fighting t, by the table at 0x4d2f20 (the mode, the distance's band, the
   sectors each lies in from the other) */
void Combat(Node *n, Node *t, int mode);
/* Weapons_Refresh 0x408c40: while +0xa714 asks, the AI block's weapons (+0xa718 the count, +0xa71c the
   instances, +0xa738 their facts, 0x20 each) and those facing forward (0x4089d0: +0xa6f4, +0xa6f8) */
void Weapons_Refresh(Node *n);
/* AI_Ram 0x41ddf0: ramming, flat out at where t will be */
void AI_Ram(Node *n, Node *t);
/* AI_Closest 0x4139f0: when p1 moving at v1 and p2 at v2 come closest; 1e7 for nearly equal velocities */
double AI_Closest(const float p1[3], const float v1[3], const float p2[3], const float v2[3]);
