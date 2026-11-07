/* The AI's set-up for a mission (0x4088a0) and its entity callback (0x408510; docs/re/ai.md): the
   vehicles by team, each vehicle's AI block (its data's 0x108) told where its vehicle is. The driving
   is not ported yet. */
#pragma once
#include "entity.h"

#define AI_TEAMS 8
extern int g_team_count[AI_TEAMS];            /* 0x505460 */
extern Entity *g_teams[AI_TEAMS][0x40];       /* 0x4edc30 */

/* AI_Rethink 0x408bf0: every team's vehicles told to choose again (their AI block's +0xa714 = 1) */
void AI_Rethink(void);
/* AI_Init 0x4088a0: the teams emptied and the callback registered (its other set-up is not ported) */
void AI_Init(void);
/* 0x505494: set when something goes for the user's vehicle this frame (0x409bc0), cleared each frame
   into 0x505490 (0x409b50); 0x409b70 reads it */
extern int g_user_pressed;
/* AI_Frame 0x409020 (each frame, unless the AI is off at 0x4a8904): the mission's script (Fsm_Frame; in
   the melee 0x408e20 instead). Not ported yet: each AI vehicle's look-ahead box (+0x9d40-+0x9d54), the
   frame's ends (0x409b50, 0x40f0a0, 0x40f050, 0x40f060, 0x40fad0) and the radar's (0x421e30) */
void AI_Frame(void);
