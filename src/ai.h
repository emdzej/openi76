/* The AI's set-up for a mission (0x4088a0) and its entity callback (0x408510; docs/re/ai.md): the
   vehicles by team, each vehicle's AI block (its data's 0x108) told where its vehicle is. The driving
   is not ported yet. */
#pragma once
#include "entity.h"

#define AI_TEAMS 8
extern int g_team_count[AI_TEAMS];            /* 0x505460 */
extern Entity *g_teams[AI_TEAMS][0x40];       /* 0x4edc30 */

/* AI_Init 0x4088a0: the teams emptied and the callback registered (its other set-up is not ported) */
void AI_Init(void);
