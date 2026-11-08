/* The CD drive's audio: Windows' MCI "cdaudio" device as the exe drives it (0x4782b0-0x478a90; docs/re/audio.md
   "The music"), over the port's CD drive. Disc 2's 16 audio tracks are in its BIN image, 2352-byte sectors of
   44.1 kHz 16-bit stereo PCM, at the places its CUE sheet gives; the gasm runner passes the BIN as the asset
   cd2 and the CUE as cd2cue. A drive without a CUE (an ISO of the data track alone) has no audio tracks, as
   a disc without them would.

   Positions are MCI's TMSF (track, minutes, seconds, frames: one byte each from the low one). The playing
   sectors are read and mixed into the output (44.1 kHz, the CD's own rate) on the audio thread, through
   the aux volume, as the drive's analogue output was. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* the CUE sheet's tracks (INDEX 01 of each); false if it has no audio track */
bool Cd_SetCue(const char *text);

/* MCI_OPEN of "cdaudio" with the time format TMSF: 0 open, 1 not (no drive, no tracks) */
int Mci_Open(void);
void Mci_Close(void);                     /* MCI_CLOSE */
/* MCI_STATUS_MODE: 0x20d stopped, 0x20e playing, 0x211 paused, 0x212 open (no disc); 0 failing */
uint32_t Mci_Mode(void);
int Mci_Tracks(void);                     /* MCI_STATUS_NUMBER_OF_TRACKS */
uint32_t Mci_TrackPos(int track);         /* MCI_STATUS_POSITION of a track (TMSF) */
uint32_t Mci_Position(void);              /* MCI_STATUS_POSITION (TMSF) */
/* MCI_PLAY from (TMSF) to (TMSF; 0 to the disc's end) */
void Mci_Play(uint32_t from, uint32_t to);
void Mci_Stop(void);                      /* MCI_STOP */
void Mci_Pause(void);                     /* MCI_PAUSE */
void Mci_Resume(void);                    /* MCI_RESUME */
/* auxSetVolume of the CD's line: both channels' 16-bit levels (left low) */
void Aux_SetVolume(uint32_t v);

/* the audio thread's: the playing sectors added into stereo frames at 44.1 kHz */
void Cd_AudioMix(float *stereo, unsigned frames);
