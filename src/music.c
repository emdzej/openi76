/* music.h: the CD's music */
#include "music.h"
#include "cdaudio.h"
#include "fsm.h"
#include "sound.h"
#include "platform.h"
#include <stdio.h>
#include <string.h>

/* ---- the drive (0x4782b0-0x478a90) ---- */

static int cd_ready;            /* 0x4dde38 */
static int cd_mode;             /* 0x5774a8: the last mode seen */
static int32_t toc[2 + 51];     /* 0x5773c0: the first track, the count, each one's position, the end's */
static uint32_t paused_at[4];   /* 0x577498: the pause's position (T, M, S, F) */

/* 0x478590: the drive's mode, 1 open, 2 stopped, 3 playing, 4 paused, 5 none */
static int cd_status(void)
{
    if (!cd_ready) return 5;
    uint32_t m = Mci_Mode();
    if (!m) {
        Music_CdClose();
        return 5;
    }
    return m == 0x20d ? 2 : m == 0x20e ? 3 : m == 0x211 ? 4 : m == 0x212 ? 1 : 5;
}

/* 0x478900: the tracks' positions; 0 read */
static int cd_toc(void)
{
    if (!cd_ready) return 1;
    int n = Mci_Tracks();
    if (n > 0x32) n = 0x32;
    toc[0] = 1, toc[1] = n;
    int t = 1;
    for (; t <= toc[1]; t++) toc[t - toc[0] + 2] = (int32_t)Mci_TrackPos(t);
    toc[t - toc[0] + 2] = (uint8_t)(toc[1] + 1);
    return 0;
}

/* 0x478a90: the aux line's volume from the level (1 is 0), 0xccc a step */
static void cd_volume(int level)
{
    if (level == 1) level = 0;
    uint32_t v = (uint32_t)level * 0xccc;
    if ((int)v > 0xfffe) v = 0xffff;
    Aux_SetVolume((v & 0xffff) | v << 16);
}

void Music_CdClose(void)   /* 0x478560 */
{
    if (!cd_ready) return;
    Mci_Close();   /* 0x4789e0 is empty, then 0x4783e0 */
    cd_ready = 0;
}

/* 0x4784b0: the device opened (0x4782b0: disc 2 in the drive, TMSF), its mode and tracks, the volume */
static int cd_open(void)
{
    if (Mci_Open()) return 0;
    cd_ready = 1;
    cd_mode = cd_status();
    switch (cd_mode) {
    case 1:
        Music_CdClose();
        return 0;
    case 2: case 3: cd_toc(); break;
    case 4: cd_toc(); { uint32_t p = Mci_Position(); for (int i = 0; i < 4; i++) paused_at[i] = p >> (8 * i) & 0xff; } break;
    }
    cd_volume(g_sound_set[0]);
    return 1;
}

static int cd_has(int t) { return toc[0] <= t && t <= toc[1]; }   /* 0x478a70 */

/* 0x4787f0: track t played to the start of track 15 (t + 1 from 15 on) */
static void cd_play(int t)
{
    if (!cd_ready) return;
    int to = t < 0xf ? 0xf : t + 1;
    int m = cd_status();
    if (m < 2 || m > 4) return;
    /* 0x478700: the position (unused here) */
    if (!cd_has(t)) return;
    if (m == 3) Mci_Stop();   /* 0x478450 */
    Mci_Play((uint32_t)toc[t - toc[0] + 2], (uint32_t)toc[to - toc[0] + 2]);
    if (m == 4) paused_at[0] = (uint32_t)t, paused_at[1] = paused_at[2] = 0;
}

/* 0x478650: as 0x4787f0 from stopped (t at most 0x32), else paused or resumed */
static void cd_toggle(int t)
{
    if (!cd_ready) return;
    int to = t < 0xf ? 0xf : t + 1;
    int m = cd_status();
    if (m == 2) {
        if (t > 0x32) t = 0x32;
        Mci_Play((uint32_t)toc[t - toc[0] + 2], (uint32_t)toc[to - toc[0] + 2]);
    } else if (m == 3) {
        uint32_t p = Mci_Position();
        for (int i = 0; i < 4; i++) paused_at[i] = p >> (8 * i) & 0xff;
        Mci_Pause();   /* 0x478410 */
    } else if (m == 4)
        Mci_Resume();   /* 0x478430 */
}

/* 0x478770: 1 when the mode changed since the last look (from open to stopped: the tracks read again) */
static int cd_changed(void)
{
    if (!cd_ready) return 0;
    int m = cd_status();
    if (cd_mode == m) return 0;
    if (m == 2 && cd_mode == 1 && cd_toc() != 0) return 0;
    cd_mode = m;
    return 1;
}

/* ---- the game's music (0x4686b0-0x468830) ---- */

static int32_t track = -1;      /* 0x4dc7a0 */
static int32_t paused;          /* 0x4dc7a4 */
static int32_t playing;         /* 0x4dc7a8 */
static int32_t in_shell;        /* 0x4dc7ac */
static uint32_t next_look;      /* 0x4dc7b0 */

void Music_SetTrack(int32_t t) { track = t; }   /* 0x4686b0 */

void Music_Play(int32_t t, int force)   /* 0x4686c0 */
{
    if (playing) return;
    Music_CdClose();
    cd_open();
    if (t != -1) track = t;
    if (!cd_ready || cd_status() == 1 || !cd_has(track)) {
        track = -1;
        return;
    }
    if (g_sound_set[0] < 2 && !force) return;
    cd_play(track);
    cd_mode = cd_status();   /* 0x478640 */
    if (cd_mode != 3) {
        track = -1;
        return;
    }
    playing = 1;
    in_shell = g_game_state == 6;
    char m[48];   /* the port's */
    snprintf(m, sizeof m, "OpenI76: the CD plays track %d", (int)track);
    plat_log(m);
}

void Music_Stop(void)   /* 0x468800 */
{
    if (playing && track != -1) {
        if (cd_ready) {   /* 0x4786e0: stopped until the drive says so */
            Mci_Stop();
        }
        playing = 0;
        paused = 0;
    }
}

void Music_Pause(void)   /* 0x468780 */
{
    if (playing && !paused) {
        if (track != -1 && g_sound_set[0] > 1) cd_toggle(0);
        paused = 1;
    }
}

void Music_Resume(void)   /* 0x4687c0 */
{
    if (playing && paused) {
        if (track != -1 && g_sound_set[0] > 1) cd_toggle(track);
        paused = 0;
    }
}

void Music_Volume(int level) { cd_volume(level); }

void Music_Frame(void)   /* 0x468830, from Sound_Frame */
{
    if (!playing) return;
    uint32_t now = (uint32_t)plat_ms();   /* GetTickCount */
    if ((int32_t)(now - next_look) <= 0) return;
    next_look = now + (in_shell ? 5000 : 60000);
    int m = cd_status();
    if (m == 1 || m == 5) {
        paused = playing = 0;
        return;
    }
    if (paused || g_sound_set[0] < 2) return;
    if (cd_changed() && track != -1) {   /* run out: in the shell the same track again, in a mission track 2 */
        cd_play(in_shell ? track : 2);
        cd_mode = cd_status();
    }
}
