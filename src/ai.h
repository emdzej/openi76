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
/* AI_Frame 0x409020 (each frame, unless the AI is off at 0x4a8904): each team vehicle's look-ahead box
   (+0x9d40-+0x9d54), the mission's script (Fsm_Frame; in the melee 0x408e20 instead), the frame's ends
   (0x409b50, 0x40f0a0, 0x40f050, 0x40f060, 0x40fad0), the user's hiding from the radar (0x421e30) */
void AI_Frame(void);
/* 0x505490: g_user_pressed as the frame began (0x409b50 moves it there and clears it) */
extern int g_user_pressed_prev;
/* AI_UserInScene 0x409b80: n is the user's vehicle while the script did not hold the controls last frame
   (outside network play) */
bool AI_UserInScene(Node *n);
/* AI_Begin 0x408cf0 (WinMain, after the load): the user's node noted (0x409b20), the flags cleared; the
   bridges' 0x4109b0 is not ported yet */
void AI_Begin(void);
/* the end of 0x409020: the search's turns for the next frame; a search not called this frame is dropped
   (0x40fdf0 on its vehicle's AI block) */
void AI_SearchTurns(void);
/* 0x419930: the tactics' test that the target t was lost: the user hides from the radar (0x421e30) and n
   has not seen t (one line of sight a frame, 0x4099e0) for more than 5 s */
int AI_TargetLost(Node *n, Node *t);
/* 0x4199a0: its opposite, 1 too while the user does not hide */
int AI_TargetSeen(Node *n, Node *t);
