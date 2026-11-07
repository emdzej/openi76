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
