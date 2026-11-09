/* The sound manager (0x466ab0-0x468970, 0x477600-0x478140; docs/re/audio.md): every sound the game makes
   is a record (0x7c bytes in the original) on one of two lists sorted by priority, the playing one (at most
   the "voices" setting of them) and the parked one (sounds waiting for room, out of range, muted, or done
   and kept loaded). A sound is a .gpw (a GAS0 header of seven words, then a RIFF WAVE) named by its .wav
   name; its buffer is made once and duplicated for each further copy. Sounds at a node are DirectSound3D
   buffers placed at the node each frame, heard from the camera; others are flat with a pan. Through the
   port's DirectSound (dsound.h). */
#pragma once
#include "scene.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct Snd Snd;
/* a record's hook: 1 at its start, 2 each frame, 3 at its end; 0 drops it there (the end: kept as is) */
typedef int (*SndFn)(Snd *s, int when);

typedef struct {           /* the caller's part of a record (+4, 0x2c bytes; 0x467f10 its defaults) */
    char name[16];         /* +0 (the name copied over it) */
    uint32_t flags;        /* +0x10: 2 an effect, 4 speech (by their volumes), 8 played on through the
                              pause, 0x10 starts in a pause, 0x200 placed once (minimum distance 100) */
    float freq;            /* +0x14: 11025 */
    float vol;             /* +0x18: 100 */
    float pan;             /* +0x1c */
    SndFn on_start, on_frame, on_end;   /* +0x20, +0x24, +0x28 */
} SndParams;

typedef struct {           /* the GAS0 header (+0x30, 0x1c bytes); -1 a default (0x467f40 all -1) */
    int32_t magic;         /* 'GAS0' */
    int32_t priority;      /* 50; the caller's is added */
    int32_t instances;     /* 1: copies of one name playing at once */
    int32_t loop;          /* 0 once, 1 looping, 2 looping while asked again each frame */
    int32_t volume;        /* 100 */
    int32_t random;        /* 1: the pitch one of ten steps 0.8-1.16 at each start */
    int32_t rate;          /* the wave's */
} SndGas;

struct Snd {
    Snd *next;             /* +0 */
    SndParams p;           /* +4 */
    SndGas gas;            /* +0x30 */
    const uint8_t *data;   /* +0x4c: the file (or the caller's, for 0x467390: its key) */
    const uint8_t *wav;    /* +0x50: its RIFF, then its samples */
    struct Dsb *dsb;       /* +0x54 */
    bool is3d;             /* +0x58: the 3D interface */
    Node *node;            /* +0x5c: referenced ("aud") */
    int32_t plays;         /* +0x60 */
    int32_t dup;           /* +0x64: a copy of another's buffer */
    int32_t f68;
    int32_t dups;          /* +0x6c: copies made of it */
    int32_t bytes;         /* +0x70 */
    uint8_t rep;           /* +0x74: asked again this frame (loop 2) */
    uint8_t f76;           /* +0x76: an engine's +8 (never read) */
    uint8_t prio8;         /* +0x77: the header's priority, as a byte */
    uint32_t state;        /* +0x78: 1 playing list, 2 parked, 4 sounding, 8 paused, 0x10 left its node,
                              0x20 bumped, 0x40 out of range, 0x80 muted, 0x100 replaced, 0x200 the data
                              the caller's; the port's 0x80000000: to be freed (by its hook) */
};

/* the shell's waves in its memory (0x467390, 0x4674a0, 0x4674f0): played (vol 100, pan 0, freq -1 the
   wave's), stopped, playing */
bool Sound_PlayData(const uint8_t *wav, int vol, int pan, int freq);
int Sound_StopData(const uint8_t *wav);
bool Sound_DataPlaying(const uint8_t *wav);
/* Sound_Init 0x466ab0 (WinMain's, once): DirectSound with 3D, the primary buffer 11,025 Hz 8-bit stereo,
   the listener, the voices and volumes from the settings, the volume table; 0 without sound */
int Sound_Init(void);
/* Sound_Close 0x466d80 */
void Sound_Close(void);
/* 0x466dd0: sound on or off (WinMain's: off while a mission loads) */
void Sound_Enable(int on);
/* Sound_Frame 0x466b70 (WinMain's each frame, with 1 in a mission): the parked readmitted, the range
   checked, the listener placed; each playing record started, ended or brought up to date */
int Sound_Frame(int full);
/* Sound_Play 0x467260: name at node (NULL: flat) with the record's params and the header's overrides;
   the record, or NULL */
Snd *Sound_Play(const char *name, Node *node, const SndParams *p, SndGas *gas);
/* Sound_Event 0x4683c0: the vehicles' sounds by number at n (NULL: the user's) */
Snd *Sound_Event(int id, Node *n, const SndParams *p);
/* 0x468580: name at n (NULL: the user's) */
Snd *Sound_PlayAt(const char *name, Node *n, const SndParams *p);
/* 0x4685d0: name at n looping while asked again each frame (loop 2) */
Snd *Sound_PlayHeld(const char *name, Node *n, const SndParams *p);
/* 0x468640: name placed once at n (flag 0x200) */
Snd *Sound_PlayPlaced(const char *name, Node *n, const SndParams *p);
/* 0x4765d0: name heard flat when n is the user's (its root flagged 0x10) */
Snd *Sound_PlayUser(const char *name, Node *n, const SndParams *p);
/* 0x467480 */
void Sound_Free(Snd *s);
/* 0x467770: the playing record of name (NULL: any) at node */
Snd *Sound_FindPlaying(const char *name, const Node *node);
/* 0x467910: the playing, then the parked */
Snd *Sound_Find(const char *name, const Node *node);
/* 0x466de0 (a mission's end): every record stopped and freed */
void Sound_StopAll(void);
/* 0x468340: the parked freed */
void Sound_FreeParked(void);
/* 0x468380: the playing stopped and parked */
void Sound_StopPlaying(void);
/* 0x4688d0 / 0x4688e0: the pause's, the effects' and the music's */
void Sound_Pause(void);
void Sound_Resume(void);
/* 0x4679a0 / 0x467a20: the effects' alone (the menus') */
void Sound_PausePlaying(void);
void Sound_ResumePlaying(void);
/* 0x467100: at most n playing (1 is 0); the speech kept */
void Sound_SetVoices(int n);
/* 0x4671d0: the music's, the effects' and the speech's volumes (-1 the settings', -2 unchanged; 1 is 0) */
void Sound_SetVolumes(int music, int sfx, int speech);
/* 0x467f10, 0x467f40: the defaults */
void Sound_ParamsInit(SndParams *p);
void Sound_GasInit(SndGas *g);
/* 0x468910 (cammo.wav) and 0x468970 (croger.wav): the effects' or the speech's volume set and heard */
void Sound_AmmoVolume(int v);
void Sound_RogerVolume(int v);

/* the vehicles' engines (0x4761b0-0x476580): Engine_Sound 0x476240 the engine's loop at n (its pitch by
   the revolutions), Engine_SoundStop 0x4763b0; Horn 0x476440 once or looping, Horn_Held 0x476420 (the
   user's key), Horn_Stop 0x476480; Siren 0x4763e0 (aps2.wav) on or off; Engine_StartTime 0x476510 the
   user's starter heard and its time, Engine_StarterStop 0x476580 */
Snd *Engine_Sound(Node *n);
void Engine_SoundStop(Node *n);
void Horn(Node *n, int loop);
void Horn_Held(Node *n);
void Horn_Stop(Node *n);
Snd *Siren(Node *n, int on);
float Engine_StartTime(Node *n);
void Engine_StarterStop(Node *n);

/* the settings' bytes 0x5e14de-0x5e14e1: the music, the effects, the speech (1 off, up to 10) and the
   voices; 0 (the bss) until Sound_Defaults */
extern uint8_t g_sound_set[4];
/* 0x44d660's: 3, 5, 10, 8 (I76PLYR.DEF, read over them, is the shell's: not ported) */
void Sound_Defaults(void);
/* the port's, for the checks: the playing and the parked records' count */
int Sound_Count(int parked);
