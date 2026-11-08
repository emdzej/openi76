/* The port's DirectSound (docs/re/audio.md "The port's DirectSound"): what the original asks of
   IDirectSound, its buffers and DirectSound3D, done in software for the gasm audio push. Sound buffers are
   PCM kept in memory (CreateSoundBuffer, DuplicateSoundBuffer, Lock/Unlock, Play, Stop, GetStatus,
   SetVolume, SetPan, SetFrequency); a 3D buffer (DSBCAPS_CTRL3D) takes its loudness, side and pitch from
   its place against the listener's (SetAllParameters, deferred until CommitDeferredSettings). The mix is
   made at the primary buffer's format (the game sets 11,025 Hz, 8 bits, stereo) and brought to the host's
   44,100 Hz. Volumes are DirectSound's: hundredths of a decibel, 0 full, -10,000 silent; pans -10,000
   left to 10,000 right as the other side's attenuation. DirectSound itself is not in the exe: how the 3D
   is heard here is the port's (inverse distance from the minimum distance, held past the maximum, a pan
   by the side, a Doppler shift). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct Dsb Dsb;

typedef struct {           /* DS3DBUFFER's fields */
    float pos[3], vel[3];
    uint32_t cone_in, cone_out;
    float cone_dir[3];
    int32_t cone_vol;
    float min_dist, max_dist;
    uint32_t mode;         /* 0 normal, 1 head-relative, 2 off */
} Ds3dBuffer;

typedef struct {           /* DS3DLISTENER's fields */
    float pos[3], vel[3], front[3], top[3];
    float distance, rolloff, doppler;
} Ds3dListener;

/* SetFormat of the primary buffer: the mix's rate, bits and channels */
void Ds_SetPrimary(uint32_t rate, int bits, int channels);
/* CreateSoundBuffer: size bytes of PCM at rate Hz, bits 8 or 16, channels 1 or 2 (flags DSBCAPS_*) */
Dsb *Dsb_Create(uint32_t flags, uint32_t rate, int bits, int channels, uint32_t size);
Dsb *Dsb_Duplicate(Dsb *b);                 /* DuplicateSoundBuffer: the same data, its own voice */
bool Dsb_Make3D(Dsb *b);                    /* QueryInterface for IDirectSound3DBuffer: its place counts */
void Dsb_Release(Dsb *b);
void Dsb_Write(Dsb *b, uint32_t at, const void *data, uint32_t n);   /* Lock, copy, Unlock */
void Dsb_Play(Dsb *b, bool loop);
void Dsb_Stop(Dsb *b);
bool Dsb_Playing(const Dsb *b);             /* GetStatus's DSBSTATUS_PLAYING */
void Dsb_SetVolume(Dsb *b, int32_t vol);
void Dsb_SetPan(Dsb *b, int32_t pan);
void Dsb_SetFrequency(Dsb *b, uint32_t hz);
void Dsb_Set3D(Dsb *b, const Ds3dBuffer *p);          /* IDirectSound3DBuffer::SetAllParameters, deferred */
void Ds_SetListener(const Ds3dListener *l);            /* IDirectSound3DListener::SetAllParameters, deferred */
void Ds_Commit(void);                                  /* CommitDeferredSettings */
void Ds_Reset(void);                                   /* every buffer released (the DirectSound object's) */
/* the port's, for the checks: each call logged as a line (buffers by their creation's serial) while on;
   Ds_LogTake hands the lines over and empties the log. Ds_Advance moves every playing buffer on by s
   seconds at its frequency, a buffer played once ending past its last frame (the checks' clock) */
void Ds_LogOn(bool on);
const char *Ds_LogTake(void);
void Ds_Advance(double s);
/* the mixer: every playing buffer into stereo floats (frames at 44,100 Hz) */
void Dsb_Mix(float *stereo, unsigned frames);
