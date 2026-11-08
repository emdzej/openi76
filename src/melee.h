/* The melee's exe side (docs/re/map.md "The instant melee"): the game's kind 3 makes a mission a melee.
   The user's car and the opponents of multicar.def are put in at the mission's spawn points, and each
   time the last opponent is gone they all come back, a little more aggressive. */
#pragma once
#include "scene.h"
#include "entity.h"
#include <stdint.h>

extern int g_melee;   /* 0x4a8760 (0x407370 reads it) */

void Melee_Set(int on);
/* 0x463140, 0x463180: the world's vehicles (the list kept by the entities' callbacks), i from 0; NULL past
   the end or at a freed entry */
Entity *Melee_Vehicle(int i);
int Melee_Vehicles(void);   /* 0x4072d0: WinMain's, by the game's kind */
/* 0x4626a0, before a melee's mission loads: no spawn points yet, the vehicles' list kept by the
   entities' callbacks (0x4631f0, 0x463260) */
void SpawnLoc_Init(void);
/* 0x462820: a random spawn point, the next one clear of every vehicle's radius (plus 0x4a63e0) */
int SpawnLoc_Get(void);
/* 0x462b90: a vehicle (name8's .vcf) built at spawn point at of kind (0 spawn, 1 regen) as an entity;
   user, the user's (the old one taken out first). NULL when it cannot be loaded */
Node *Spawn_Vehicle(int at, int kind, const uint8_t name8[8], int user);
/* WinMain's after the terrain's textures, in a melee: the user's car from its .vcf, then the opponents
   (Opponents_Load 0x407070) */
void Melee_Start(void);
void Melee_Frame(void);   /* 0x408e20 */
/* 0x463670: NSCR.DAT, the user's score (name and car the mission record's) */
void Melee_Results(const char *name, const char *car);
void Melee_End(void);   /* 0x4072b0 at the mission's end */
void Melee_Kill(Node *victim);  /* 0x4072e0: a kill by the user */
void Melee_Score(int32_t *points, int16_t *kills);   /* 0x407340 */
/* Vehicle_TakeOut 0x42b7d0: a vehicle out of the world (its sounds, entity, weapons and node) */
/* the scores' box: Scores_Init 0x4632c0, Scores_Fill 0x463350, Scores_Toggle 0x4635d0 (SCORE_TOGGLE) */
void Scores_Init(void);
void Scores_Fill(void);
void Scores_Toggle(void);
void Scores_Show(int on);   /* 0x463590 */
/* network play's cars (0x462930), moves (0x462d30), the spawn points cleared (0x463130), and the regen
   points' frame (0x462dd0, the melee's and network play's) */
Node *Net_SpawnCar(uint16_t id, int at, int kind, const uint8_t *data, int len);
void Spawn_Move(Node *n, int at);
void Spawn_Clear(void);
int Regen_Frame(void);
struct Entity;
void Vehicle_TakeOut(struct Entity *e);
