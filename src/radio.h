/* The CB radio (0x46fa50-0x46fdb0; docs/re/audio.md "The CB radio"): the script's and the game's spoken
   messages wait in one queue (0x4dd904) and play one at a time as speech at priority 10,000, flat; each one's
   end plays the next. A message by kind: 1 empties the queue first, 2 cuts the one playing, 4 is dropped
   when it waited over 10 s, 5 only plays on an empty queue, 6 not behind one of kinds below 5; a kind below
   5 empties a queue that plays one of 5 or more. With the speech or the voices off (under 2) nothing is
   queued. A message from the user's vehicle calls the handset up while it plays. */
#pragma once
#include "scene.h"
#include <stdbool.h>

/* Radio_Say 0x46fa70: name from node (NULL: none) of kind; true when it plays now (or the speech is off).
   "STOPCBXX" mutes the radio (0x46fa50) */
bool Radio_Say(const char *name, Node *node, int kind);
/* Radio_Clear 0x46fd20: the queue emptied (keep: but the one playing) */
void Radio_Clear(int keep);
/* Radio_Empty 0x46fda0 */
bool Radio_Empty(void);
/* Radio_Mute 0x46fa50: 1 the radio on (WinMain's each mission), 0 off */
void Radio_On(int on);
/* Radio_Drop 0x46fdb0 (a vehicle's death): node's messages dropped; the one playing cut by cmike.wav */
void Radio_Drop(Node *node);
/* the port's, for the checks: the queue's length */
int Radio_Count(void);
/* the poetry: 0x451320 at each mission's start, the poems and the user's answers shuffled, then (WinMain's
   0x4513a0) t04 and t06 have Taurus decline, t05 nothing */
void Poetry_Init(const char *mission);
/* 0x4513c0, the poetry key: on an empty CB queue the user asks (GDSGC40), Taurus recites the next poem and
   the user answers, each list shuffled again once gone through */
void Poetry_Ask(Node *user);
