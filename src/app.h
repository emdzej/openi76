/* The application per frame: the original runs a Win32 message loop around its game frame (to be located
   in the exe); gasm calls app_frame once per frame instead. A frame never
   blocks. */
#pragma once
#include <stdbool.h>

#define APP_FRAME_HZ 60.0
#define APP_AUDIO_RATE 44100

bool app_init(void);        /* data mounted: check the exe, set up the driver; false = fatal (logged) */
bool app_frame(void);       /* one frame; false = the game quit */
void app_audio(float *stereo, unsigned frames);
void app_exit(void);
