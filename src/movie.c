/* movie.h: the movies, as the exe plays them full screen (its DirectDraw path; the windowed one, through a
   SmackBuffer and its blits, is not the port's) */
#include "movie.h"
#include "cd.h"
#include "clock.h"
#include "dsound.h"
#include "platform.h"
#include "smacker.h"
#include "sound.h"
#include "world.h"
#include "render/palette.h"
#include <stdio.h>
#include <string.h>

uint32_t g_play_prev = 1;    /* 0x4db00c */
uint8_t g_black;             /* 0x58dcbc: the palette's black */

static Smk *smk;             /* 0x4dafa8: the movie */
static bool sound_on;        /* 0x4dafb0 */
static uint8_t *screen;      /* the display record's surface 0x58d870 */
static int screen_pitch;
static uint8_t shown[768];   /* the hardware palette while a movie plays */

/* the port's: the movie's track goes through a buffer of its own (smackw32's, on the game's DirectSound),
   a ring of four seconds the frames' samples are written into as they are decoded */
#define RING_SECONDS 4
static Dsb *track;
static uint32_t ring_bytes, ring_at;

void Movie_SetScreen(uint8_t *pixels, int pitch) { screen = pixels, screen_pitch = pitch; }

/* 0x44f7d0 */
void Play_SetState(uint32_t s)
{
    g_play_state = s == g_play_state ? 1 : s;
}

/* 0x44e6b0: the movies' sound on the game's DirectSound (the port's ring, made as a movie opens) */
void Movie_SoundUse(void) {}

static double now(void) { return g_clock_fake_ms >= 0 ? g_clock_fake_ms : plat_ms(); }

static void track_open(void)
{
    int rate = Smk_AudioRate(smk);
    if (!rate) return;
    int bits = Smk_AudioBits(smk), ch = Smk_AudioChannels(smk);
    ring_bytes = (uint32_t)rate * (uint32_t)(bits / 8 * ch) * RING_SECONDS;
    ring_at = 0;
    track = Dsb_Create(0, (uint32_t)rate, bits, ch, ring_bytes);
}

static void track_feed(void)
{
    uint32_t n;
    const uint8_t *p = Smk_Audio(smk, &n);
    if (!track || !n) return;
    while (n) {
        uint32_t k = ring_bytes - ring_at < n ? ring_bytes - ring_at : n;
        Dsb_Write(track, ring_at, p, k);
        p += k, n -= k;
        ring_at = (ring_at + k) % ring_bytes;
    }
    if (!Dsb_Playing(track)) Dsb_Play(track, true);
}

static void track_close(void)
{
    if (!track) return;
    Dsb_Stop(track);
    Dsb_Release(track);
    track = NULL;
}

/* SmackClose with the screen put back (0x437d90: cleared to black, shown, the game's palette set again) and
   the primary buffer at 11,025 Hz */
static void close_movie(void)
{
    if (smk) {
        char m[96];
        snprintf(m, sizeof m, "OpenI76: the movie closed at frame %u of %u", Smk_FrameNum(smk), Smk_Frames(smk));
        plat_log(m);
    }
    track_close();
    Smk_Close(smk);
    smk = NULL;
    Play_SetState(1);
    if (screen) {
        for (int y = 0; y < 480; y++) memset(screen + (size_t)y * (size_t)screen_pitch, g_black, 640);
        plat_present(screen, 640, 480, screen_pitch, g_shown_pal);
    }
    Ds_SetPrimary(0x2b11, 8, 2);   /* 0x477ff0 */
}

/* 0x44e6e0. The path is the CD's drive with \cutscene\, or the local smk\ without a CD drive (0x484bc0);
   the port's file layer has the disc at its root. The open's flags (0x60, with 0x2000 or 0x4000 by the
   file's attributes from 0x44f2b0) and its buffer size (0xfa000) are smackw32's. The force feedback's off (0x468a50, none in the port) and
   the clearing of the mouse's and keys' messages are not ported */
bool Movie_Play(const char *name)
{
    if (smk) return false;
    Ds_SetPrimary(0x5622, 8, 2);   /* 0x477ff0 */
    char path[96];
    snprintf(path, sizeof path, "%sCUTSCENE/%s", Cd_Root(), name);
    if (!(smk = Smk_Open(path))) {
        char m[200];
        snprintf(m, sizeof m, "OpenI76: the movie %s cannot be opened", path);
        plat_log(m);
        close_movie();
        return false;
    }
    char m[160];
    snprintf(m, sizeof m, "OpenI76: the movie %s, %u frames", path, Smk_Frames(smk));
    plat_log(m);
    /* the display's mode: 5 (640 x 480), or 9 for a 320 wide movie; every movie on the discs is 640 x 480 */
    track_open();
    sound_on = true;
    Play_SetState(0x20);
    Sound_Pause();   /* 0x4688d0 */
    return true;
}

/* 0x44ed50 */
bool Movie_Playing(void) { return smk != NULL; }

/* 0x44ed90, its full-screen path (0x4d66e0 set): the frame decoded onto the locked surface and shown; with a
   new palette, it through the gamma (0x43c170) as the hardware palette's 256 entries */
bool Movie_Frame(void)
{
    if (!smk) return false;
    if (Smk_Wait(smk, now())) return true;
    Smk_DoFrame(smk);
    track_feed();
    if (screen) {
        const uint8_t *p = Smk_Pixels(smk);
        int w = Smk_Width(smk), h = Smk_Height(smk);
        for (int y = 0; y < h && y < 480; y++) memcpy(screen + (size_t)y * (size_t)screen_pitch, p + (size_t)y * (size_t)w, (size_t)(w < 640 ? w : 640));
    }
    if (Smk_NewPalette(smk)) {
        memcpy(shown, Smk_Palette(smk), sizeof shown);
        Gamma_Apply(shown, 0, 256, 0);
    }
    /* the hardware palette is the frame's (0x4dafb4 asks for it to be set again after a pause) */
    if (screen) plat_present(screen, 640, 480, screen_pitch, shown);
    if (Smk_Frames(smk) - Smk_FrameNum(smk) == 1) {
        close_movie();
        Ds_SetPrimary(0x2b11, 8, 2);
        Sound_Resume();   /* 0x4688e0; the force feedback's 0x468a40: none in the port */
        return false;
    }
    Smk_NextFrame(smk);
    return true;
}

/* 0x44f090 */
void Movie_End(void)
{
    if (smk) close_movie();
    else {
        Play_SetState(1);
        Ds_SetPrimary(0x2b11, 8, 2);
    }
}

/* 0x44f170 */
void Movie_Skip(void)
{
    Movie_End();
    Ds_SetPrimary(0x2b11, 8, 2);
    Sound_Resume();   /* 0x4688e0; the force feedback's 0x468a40: none in the port */
}

/* 0x44f570: SmackSoundOnOff off */
bool Movie_SoundOff(void)
{
    if (!smk) return sound_on = false;
    if (track) Dsb_Stop(track);
    return sound_on = true;
}

/* 0x44f5a0: the palette set again, SmackSoundOnOff on */
bool Movie_SoundOn(void)
{
    if (!smk) return sound_on = false;
    if (track) Dsb_Play(track, true);
    return sound_on = true;
}

/* 0x44fc00 */
void Pause_Toggle(void)
{
    if (g_play_state != 2) {
        if (!Movie_SoundOff() && g_play_prev == 1) Sound_Pause();   /* and the force feedback off, 0x468a50 */
        g_play_prev = g_play_state;
        g_play_state = 2;
        return;
    }
    if (!Movie_SoundOn() && g_play_prev == 1) Sound_Resume();   /* and the force feedback on, 0x468a40 */
    g_play_state = g_play_state == g_play_prev ? 1 : g_play_prev;
}
