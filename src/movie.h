/* The movies (0x44e6b0-0x44fc00; docs/re/movies.md): a Smacker movie of CUTSCENE\ played full screen while
   the play's state 0x4db008 is 0x20, a frame each pass of WinMain's loop, over the game's sounds (paused),
   with its own palette and its track through the port's DirectSound at 22,050 Hz. Esc or a click skips it. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

extern uint32_t g_play_prev;   /* 0x4db00c: the state the pause returns to */
extern uint8_t g_black;        /* 0x58dcbc: the palette's black (WinMain's Color_Match(0, 0, 0)) */

void Play_SetState(uint32_t s);   /* 0x44f7d0: s, or 1 when the state is s already */
void Pause_Toggle(void);          /* 0x44fc00: the pause on and off (the state 2) */
void Movie_SoundUse(void);        /* 0x44e6b0: SmackSoundUseDirectSound */
/* 0x44e6e0: opens <name> of CUTSCENE\ and starts it (the state 0x20, the game's sounds paused); false if a
   movie is open already or it cannot be opened */
bool Movie_Play(const char *name);
bool Movie_Playing(void);         /* 0x44ed50 */
/* 0x44ed90: the next frame when it is due, shown with its palette; at the last, the movie closed and the
   game's sounds back. False once it is over */
bool Movie_Frame(void);
void Movie_End(void);             /* 0x44f090: closed (WinMain's, the sounds left paused) */
void Movie_Skip(void);            /* 0x44f170: Esc or a click: closed, the sounds back */
bool Movie_SoundOff(void);        /* 0x44f570: true with a movie open */
bool Movie_SoundOn(void);         /* 0x44f5a0 */
/* the frame the movie shows: the screen, 640 x 480 (the display record's surface) */
void Movie_SetScreen(uint8_t *pixels, int pitch);
