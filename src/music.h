/* The music: CD audio tracks of disc 2 (0x4686b0-0x468910 over the drive's 0x4782b0-0x478a90; docs/re/audio.md
   "The music"). A mission's world names its track (WRLD +8), played after the mission's movie to the start of
   track 15; the shell plays track 15. The drive is looked at every minute (5 s in the shell): when the music
   has run out, the shell's starts again, a mission's from track 2. The level (the settings' 0x5e14de) sets the
   CD line's volume; under 2 there is no music. */
#pragma once
#include <stdint.h>

void Music_SetTrack(int32_t t);       /* 0x4686b0 */
/* 0x4686c0: the drive opened and the track (-1: the one set) played; force plays it under level 2 too */
void Music_Play(int32_t t, int force);
void Music_Stop(void);                /* 0x468800 */
void Music_Pause(void);               /* 0x468780 */
void Music_Resume(void);              /* 0x4687c0 */
void Music_Frame(void);               /* 0x468830 */
void Music_Volume(int level);         /* 0x478a90 */
void Music_CdClose(void);             /* 0x478560 */
