/* The port's smackw32.dll: the Smacker movies the game plays (CUTSCENE\*.SMK, disc 1's introf01 and
   credf01 and disc 2's 29; all version 2, 640 x 480 at 15 frames a second, one 8-bit mono track of
   22,050 Hz). The container, the four Huffman trees the header carries, the palette's deltas, the video's
   4 x 4 blocks of four kinds and the audio's DPCM are written from the format (docs/formats/smk.md); the
   one table the codec takes from the DLL, the palette's 6-to-8-bit ramp, is read from the user's
   smackw32.dll (disc 1's, 71,680 bytes, CRC-32 f1e9c672) at run time, as exe.c does for I76.EXE. Only
   what the exe calls is here: SmackOpen, SmackDoFrame, SmackNextFrame, SmackWait, SmackClose and the
   fields it reads (the size, the frame counts, the new palette); the decoder draws into its own 8-bit
   frame, which the caller copies where SmackToBuffer would have put it. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SMK_DLL "SMACKW32.DLL"

typedef struct Smk Smk;

bool Smk_LoadTables(char *err, size_t cap);   /* smackw32.dll through the file layer; idempotent */
Smk *Smk_Open(const char *path);              /* SmackOpen: NULL if missing or not a version 2 movie */
void Smk_Close(Smk *s);                       /* SmackClose */
int Smk_Width(const Smk *s);                  /* +4 */
int Smk_Height(const Smk *s);                 /* +8 */
uint32_t Smk_Frames(const Smk *s);            /* +0xc */
uint32_t Smk_FrameNum(const Smk *s);          /* +0x374: the current frame, from 0 */
double Smk_FrameMs(const Smk *s);             /* a frame's time, from the header's rate */
/* SmackDoFrame: the current frame's palette, audio and video; false on a damaged frame (the picture is
   left as far as it got) */
bool Smk_DoFrame(Smk *s);
void Smk_NextFrame(Smk *s);                   /* SmackNextFrame */
/* SmackWait: true while the current frame is not yet due by now_ms (the clock starts at the first call) */
bool Smk_Wait(Smk *s, double now_ms);
const uint8_t *Smk_Pixels(const Smk *s);      /* the frame: width x height bytes */
bool Smk_NewPalette(const Smk *s);            /* +0x68: the last DoFrame changed the palette */
const uint8_t *Smk_Palette(const Smk *s);     /* +0x6c: 256 x RGB, 8 bits */
/* the audio track 0: its rate (0 without one), bits and channels, and the samples the last DoFrame
   decoded (unsigned 8-bit or signed 16-bit, interleaved) */
int Smk_AudioRate(const Smk *s);
int Smk_AudioBits(const Smk *s);
int Smk_AudioChannels(const Smk *s);
const uint8_t *Smk_Audio(const Smk *s, uint32_t *bytes);
