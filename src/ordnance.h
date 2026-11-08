/* The shots (0x458f40-0x45d710; docs/re/weapons.md "The launch"): each ordnance kind's pool of shot nodes
   (class 0x33, a 0x48-byte record in their data), its flying shots (list 0, the kind's node's children;
   list 1 at +0x10) and the launch. */
#pragma once
#include "weapons.h"

extern Node *g_user_shot;    /* 0x573484 */
extern int32_t g_shots_off;  /* 0x570aa4 */
/* the shot's flight for dt (0x459b50): 0 when it is spent */
extern int (*g_shot_fly)(Node *s, float dt);

/* Ordnance_Index 0x45c920: the kind's place in the table, -1 for none */
int Ordnance_Index(int32_t type);
/* Shot_Target 0x45d710: where a shot aims at t: its entity's centre now, for a structure 0.6 m above the
   ground under it */
void Shot_Target(float out[3], Node *t);
/* Shot_Launch 0x458f40: l's shot from its kind's pool (made at the first, 50), placed at the muzzle
   (0x459e40), moved back by the vehicle's motion over late, its record set, then flown for late; types
   9-11 are dropped one by one instead (0x46d090), 0xd does nothing */
Node *Shot_Launch(Launch *l, float late);
/* 0x45cb60 (the flight's state gathered: the target's point kept at the record's +0x20): its target */
Node *Shot_Gather(Node *s);
/* Shot_Sweep 0x46a9b0: the shot's move to p1 against the entities but owner (moving, then static: their
   ground boxes, a sphere sweep of their radius within the frame, then closely without margin, faces
   too); on a hit p1 is cut there, *who what was hit (a helicopter as itself), *cls its class (-1 none),
   and the impact's damage done (0x45d7c0; a kill clears *who). 1 on a hit */
int Shot_Sweep(Node *s, Node *owner, float p1[3], Node **who, int32_t *cls);
/* Shot_Fly 0x459b50: the shot's flight for t by its type; 0 when spent */
int Shot_Fly(Node *s, float t);
/* Shots_Frame 0x4597a0: every live shot of every kind (its node's children, then its second list) flown
   for the frame's step; the spent back to their pool */
void Shots_Frame(void);
/* Vehicle_Kick 0x476020: n kicked, spun by the quarter q's turn rates times spin, its velocity times damp
   when above 2 m/s, then up by up (at most 20 m/s); Vehicle_KickRandom 0x476170: a random quarter, up x 6 */
void Vehicle_Kick(Node *n, int q, float spin, float up, float damp);
void Vehicle_KickRandom(Node *n, float spin, float up);
