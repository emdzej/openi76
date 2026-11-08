/* cdaudio.h: the CD drive's audio behind MCI */
#include "cdaudio.h"
#include "exe.h"
#include "vfs.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { SECTOR = 2352, FRAMES_PER_SECTOR = SECTOR / 4, MAX_TRACKS = 99 };

static int ntracks;                       /* the CUE's tracks, 1..ntracks */
static uint32_t track_start[MAX_TRACKS + 2]; /* each one's first sector (INDEX 01) */
static bool track_audio[MAX_TRACKS + 2];
static bool open_;

/* the play, shared with the audio thread: the sectors [pos, end) in frames, and the state */
static _Atomic uint64_t play_pos, play_end;
static _Atomic int play_state;            /* 0 stopped, 1 playing, 2 paused */
static _Atomic uint32_t volume = 0xffffffffu;

bool Cd_SetCue(const char *text)
{
    ntracks = 0;
    int cur = 0, any = 0;
    for (const char *l = text; l && *l;) {
        int t, m, s, f;
        char kind[16];
        while (*l == ' ' || *l == '\t') l++;
        if (sscanf(l, "TRACK %d %15s", &t, kind) == 2 && t >= 1 && t <= MAX_TRACKS) {
            cur = t;
            track_audio[t] = !strcmp(kind, "AUDIO");
            any |= track_audio[t];
            if (t > ntracks) ntracks = t;
        } else if (cur && sscanf(l, "INDEX 01 %d:%d:%d", &m, &s, &f) == 3)
            track_start[cur] = (uint32_t)((m * 60 + s) * 75 + f);
        l = strchr(l, '\n');
        if (l) l++;
    }
    return any;
}

/* a track's end: the next one's start, or the image's */
static uint32_t track_end(int t)
{
    if (t < ntracks) return track_start[t + 1];
    return (uint32_t)(vfs_drive_size() / SECTOR);
}

/* 0x484cf0's condition: the drive holds disc 2 (I76_CD2, 0x4e9b74), and it has audio */
int Mci_Open(void)
{
    const char *label = vfs_drive_label();
    if (!label || strcmp(label, exe_str(0x4e9b74)) || !ntracks) return 1;
    open_ = true;
    return 0;
}

void Mci_Close(void)
{
    atomic_store(&play_state, 0);
    open_ = false;
}

uint32_t Mci_Mode(void)
{
    if (!open_) return 0;
    if (!vfs_drive_label()) return 0x212;
    int s = atomic_load(&play_state);
    if (s == 1 && atomic_load(&play_pos) >= atomic_load(&play_end)) s = 0;
    return s == 1 ? 0x20e : s == 2 ? 0x211 : 0x20d;
}

int Mci_Tracks(void) { return ntracks; }

uint32_t Mci_TrackPos(int track) { return (uint32_t)track & 0xff; }

uint32_t Mci_Position(void)
{
    uint64_t sec = atomic_load(&play_pos) / FRAMES_PER_SECTOR;
    int t = 1;
    while (t < ntracks && sec >= track_start[t + 1]) t++;
    uint32_t in = (uint32_t)(sec - (sec >= track_start[t] ? track_start[t] : sec));
    return (uint32_t)t | (in / 75 / 60 % 256) << 8 | (in / 75 % 60) << 16 | (in % 75) << 24;
}

static uint64_t tmsf_frame(uint32_t p)
{
    int t = (int)(p & 0xff);
    if (t < 1) t = 1;
    if (t > ntracks) return (uint64_t)track_end(ntracks) * FRAMES_PER_SECTOR;
    uint32_t sec = track_start[t] + ((p >> 8 & 0xff) * 60 + (p >> 16 & 0xff)) * 75 + (p >> 24 & 0xff);
    return (uint64_t)sec * FRAMES_PER_SECTOR;
}

void Mci_Play(uint32_t from, uint32_t to)
{
    if (!open_) return;
    uint64_t a = tmsf_frame(from), b = to ? tmsf_frame(to) : (uint64_t)track_end(ntracks) * FRAMES_PER_SECTOR;
    atomic_store(&play_state, 0);
    atomic_store(&play_pos, a);
    atomic_store(&play_end, b);
    atomic_store(&play_state, 1);
}

void Mci_Stop(void) { atomic_store(&play_state, 0); }

void Mci_Pause(void)
{
    if (atomic_load(&play_state) == 1) atomic_store(&play_state, 2);
}

void Mci_Resume(void)
{
    if (atomic_load(&play_state) == 2) atomic_store(&play_state, 1);
}

void Aux_SetVolume(uint32_t v) { atomic_store(&volume, v); }

void Cd_AudioMix(float *stereo, unsigned frames)
{
    if (atomic_load(&play_state) != 1) return;
    uint64_t pos = atomic_load(&play_pos), end = atomic_load(&play_end);
    uint32_t v = atomic_load(&volume);
    float gl = (float)(v & 0xffff) / 65535.0f, gr = (float)(v >> 16) / 65535.0f;
    static int16_t buf[FRAMES_PER_SECTOR * 2 * 8];
    unsigned done = 0;
    while (done < frames && pos < end) {
        uint64_t sec = pos / FRAMES_PER_SECTOR;
        unsigned in = (unsigned)(pos % FRAMES_PER_SECTOR);
        unsigned want = frames - done;
        uint64_t left = end - pos;
        if (want > left) want = (unsigned)left;
        unsigned avail = 8 * FRAMES_PER_SECTOR - in;
        if (want > avail) want = avail;
        int64_t got = vfs_drive_read_raw(sec * SECTOR, buf, 8 * SECTOR);
        if (got <= 0) break;
        unsigned have = (unsigned)(got / 4) > in ? (unsigned)(got / 4) - in : 0;
        if (want > have) want = have;
        if (!want) break;
        for (unsigned i = 0; i < want; i++) {   /* little-endian samples, as on the disc */
            stereo[2 * (done + i)] += buf[2 * (in + i)] / 32768.0f * gl;
            stereo[2 * (done + i) + 1] += buf[2 * (in + i) + 1] / 32768.0f * gr;
        }
        done += want, pos += want;
    }
    if (atomic_load(&play_state) == 1) atomic_store(&play_pos, pos);
}
