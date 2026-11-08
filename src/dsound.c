/* The port's DirectSound (dsound.h). */
#include "dsound.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MAX_BUFFERS = 512, OUT_RATE = 44100 };

typedef struct {
    int16_t *pcm;      /* the data as 16-bit frames (mono or stereo interleaved) */
    uint32_t frames;
    int refs;
} Data;

struct Dsb {
    Data *d;
    int channels, bits;
    uint32_t freq;
    double pos;        /* in frames */
    bool playing, loop, used, is3d;
    int32_t vol, pan;
    Ds3dBuffer p3, next3;   /* in force, and as set (deferred) */
    bool set3;
    uint32_t serial;
};

static Dsb bufs[MAX_BUFFERS];
static Ds3dListener listener = {{0, 0, 0}, {0, 0, 0}, {0, 0, 1}, {0, 1, 0}, 1, 1, 1},
                    next_listener = {{0, 0, 0}, {0, 0, 0}, {0, 0, 1}, {0, 1, 0}, 1, 1, 1};
static uint32_t prim_rate = 22050;   /* DirectSound's default primary format */
static int prim_bits = 8;
static uint32_t serial;

static bool log_on;
static char *log_buf;
static size_t log_len, log_cap;

static void dlog(const char *fmt, ...)
{
    if (!log_on) return;
    char m[400];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(m, sizeof m, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (log_len + (size_t)n + 2 > log_cap) {
        size_t cap = (log_cap + (size_t)n + 2) * 2;
        char *b = realloc(log_buf, cap);
        if (!b) return;
        log_buf = b, log_cap = cap;
    }
    memcpy(log_buf + log_len, m, (size_t)n);
    log_len += (size_t)n;
    log_buf[log_len++] = ';';
    log_buf[log_len] = 0;
}

void Ds_LogOn(bool on) { log_on = on, log_len = 0; if (log_buf) log_buf[0] = 0; }

const char *Ds_LogTake(void)
{
    static char *out;
    free(out);
    out = log_buf ? strdup(log_buf) : strdup("");
    log_len = 0;
    if (log_buf) log_buf[0] = 0;
    return out;
}

static uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t c = 0xffffffffu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = c >> 1 ^ (0xedb88320u & (0u - (c & 1)));
    }
    return ~c;
}

static void hexf(char *o, const float *f, int n)
{
    for (int i = 0; i < n; i++) {
        uint32_t u;
        memcpy(&u, &f[i], 4);
        sprintf(o + 9 * i, "%08x,", u);
    }
}

void Ds_SetPrimary(uint32_t rate, int bits, int channels)
{
    (void)channels;
    prim_rate = rate < 4000 ? 4000 : rate > OUT_RATE ? OUT_RATE : rate;
    prim_bits = bits == 8 ? 8 : 16;
    dlog("fmt %u %d %d", rate, bits, channels);
}

static Dsb *alloc_buf(void)
{
    for (int i = 0; i < MAX_BUFFERS; i++)
        if (!bufs[i].used) {
            memset(&bufs[i], 0, sizeof bufs[i]);
            bufs[i].used = true;
            return &bufs[i];
        }
    return NULL;
}

static void default3d(Dsb *b)
{
    Ds3dBuffer p = {{0, 0, 0}, {0, 0, 0}, 360, 360, {0, 0, 1}, 0, 1.0f, 1e9f, 0};
    b->p3 = b->next3 = p;
}

Dsb *Dsb_Create(uint32_t flags, uint32_t rate, int bits, int channels, uint32_t size)
{
    Dsb *b = alloc_buf();
    if (!b) return NULL;
    b->d = calloc(1, sizeof *b->d);
    if (!b->d) return b->used = false, NULL;
    b->d->refs = 1;
    b->bits = bits == 8 ? 8 : 16;
    b->channels = channels == 2 ? 2 : 1;
    b->d->frames = size / (uint32_t)(b->bits / 8 * b->channels);
    b->d->pcm = calloc((size_t)b->d->frames * (size_t)b->channels + 1, sizeof(int16_t));
    b->freq = rate;
    default3d(b);
    b->serial = serial++;
    dlog("create %u %x %u %u %d %d", b->serial, flags, size, rate, bits, channels);
    return b;
}

bool Dsb_Make3D(Dsb *b)
{
    if (!b) return false;
    b->is3d = true;
    dlog("qi %u", b->serial);
    return true;
}

Dsb *Dsb_Duplicate(Dsb *s)
{
    if (!s) return NULL;
    Dsb *b = alloc_buf();
    if (!b) return NULL;
    *b = *s;
    b->used = true;
    b->playing = false;
    b->pos = 0;
    b->is3d = false;
    default3d(b);
    b->d->refs++;
    b->serial = serial++;
    dlog("dup %u %u", b->serial, s->serial);
    return b;
}

void Dsb_Release(Dsb *b)
{
    if (!b || !b->used) return;
    dlog("rel %u", b->serial);
    if (--b->d->refs == 0) {
        free(b->d->pcm);
        free(b->d);
    }
    b->used = false;
}

void Ds_Reset(void)
{
    for (int i = 0; i < MAX_BUFFERS; i++) Dsb_Release(&bufs[i]);
}

void Ds_Advance(double s)
{
    for (int i = 0; i < MAX_BUFFERS; i++) {
        Dsb *b = &bufs[i];
        if (!b->used || !b->playing) continue;
        b->pos += s * b->freq;
        if (b->pos >= b->d->frames) {
            if (b->loop && b->d->frames) b->pos = fmod(b->pos, b->d->frames);
            else b->playing = false, b->pos = 0;
        }
    }
}

void Dsb_Write(Dsb *b, uint32_t at, const void *data, uint32_t n)
{
    if (!b) return;
    const uint8_t *p = data;
    uint32_t bps = (uint32_t)(b->bits / 8), total = b->d->frames * (uint32_t)b->channels;
    uint32_t s0 = at / bps;
    for (uint32_t i = 0; i < n / bps && s0 + i < total; i++) {
        int16_t v = b->bits == 8 ? (int16_t)((p[i] - 128) << 8) : (int16_t)(p[2 * i] | p[2 * i + 1] << 8);
        b->d->pcm[s0 + i] = v;
    }
    dlog("fill %u %08x", b->serial, crc32(p, n));
}

void Dsb_Play(Dsb *b, bool loop) { if (b) b->playing = true, b->loop = loop, dlog("play %u %d", b->serial, loop); }
void Dsb_Stop(Dsb *b) { if (b) b->playing = false, dlog("stop %u", b->serial); }
bool Dsb_Playing(const Dsb *b) { return b && b->playing; }
void Dsb_SetVolume(Dsb *b, int32_t v) { if (b) b->vol = v < -10000 ? -10000 : v > 0 ? 0 : v, dlog("vol %u %d", b->serial, v); }
void Dsb_SetPan(Dsb *b, int32_t p) { if (b) b->pan = p < -10000 ? -10000 : p > 10000 ? 10000 : p, dlog("pan %u %d", b->serial, p); }
void Dsb_SetFrequency(Dsb *b, uint32_t hz)
{
    if (b) b->freq = hz < 100 ? 100 : hz > 100000 ? 100000 : hz, dlog("freq %u %u", b->serial, hz);
}

void Dsb_Set3D(Dsb *b, const Ds3dBuffer *p)
{
    if (!b) return;
    b->next3 = *p, b->set3 = true;
    char h[16 * 9 + 1] = {0};
    float f[15] = {p->pos[0], p->pos[1], p->pos[2], p->vel[0], p->vel[1], p->vel[2], 0, 0,
                   p->cone_dir[0], p->cone_dir[1], p->cone_dir[2], 0, p->min_dist, p->max_dist, 0};
    memcpy(&f[6], &p->cone_in, 4), memcpy(&f[7], &p->cone_out, 4), memcpy(&f[11], &p->cone_vol, 4);
    memcpy(&f[14], &p->mode, 4);
    hexf(h, f, 15);
    dlog("set3d %u %s", b->serial, h);
}

void Ds_SetListener(const Ds3dListener *l)
{
    next_listener = *l;
    char h[16 * 9 + 1] = {0};
    float f[15] = {l->pos[0], l->pos[1], l->pos[2], l->vel[0], l->vel[1], l->vel[2], l->front[0], l->front[1],
                   l->front[2], l->top[0], l->top[1], l->top[2], l->distance, l->rolloff, l->doppler};
    hexf(h, f, 15);
    dlog("lis %s", h);
}

void Ds_Commit(void)
{
    dlog("commit");
    listener = next_listener;
    for (int i = 0; i < MAX_BUFFERS; i++)
        if (bufs[i].used && bufs[i].set3) bufs[i].p3 = bufs[i].next3, bufs[i].set3 = false;
}

static float db_gain(int32_t att) { return att <= -10000 ? 0.0f : powf(10.0f, (float)att / 2000.0f); }

static float dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

/* a 3D buffer's gain, its left and right shares and its pitch's factor (the port's) */
static void place(const Dsb *b, float *g, float *gl, float *gr, double *pitch)
{
    *g = 1, *gl = *gr = 1, *pitch = 1;
    const Ds3dBuffer *p = &b->p3;
    if (!b->is3d || p->mode == 2) return;
    const Ds3dListener *l = &listener;
    float rel[3], right[3];
    if (p->mode == 1) memcpy(rel, p->pos, sizeof rel);
    else {
        float d[3] = {p->pos[0] - l->pos[0], p->pos[1] - l->pos[1], p->pos[2] - l->pos[2]};
        /* into the listener's axes: x right (top x front), y up, z ahead */
        right[0] = l->top[1] * l->front[2] - l->top[2] * l->front[1];
        right[1] = l->top[2] * l->front[0] - l->top[0] * l->front[2];
        right[2] = l->top[0] * l->front[1] - l->top[1] * l->front[0];
        rel[0] = dot(d, right), rel[1] = dot(d, l->top), rel[2] = dot(d, l->front);
    }
    float dist = sqrtf(dot(rel, rel)) * (l->distance > 0 ? l->distance : 1);
    float mn = p->min_dist > 0 ? p->min_dist : 1, mx = p->max_dist > mn ? p->max_dist : mn;
    float dd = dist > mx ? mx : dist;
    if (dd > mn) *g = mn / (mn + l->rolloff * (dd - mn));
    if (dist > 1e-3f) {
        float side = rel[0] / sqrtf(dot(rel, rel));   /* -1 left .. 1 right */
        *gl = side > 0 ? 1 - 0.8f * side : 1;
        *gr = side < 0 ? 1 + 0.8f * side : 1;
        if (p->mode == 0 && l->doppler > 0) {
            float u[3] = {rel[0], rel[1], rel[2]}, n = sqrtf(dot(u, u));
            float d[3] = {p->pos[0] - l->pos[0], p->pos[1] - l->pos[1], p->pos[2] - l->pos[2]};
            for (int i = 0; i < 3; i++) d[i] /= n;
            const float c = 343.0f;
            double f = (c + l->doppler * dot(l->vel, d)) / (c + l->doppler * dot(p->vel, d));
            *pitch = f < 0.5 ? 0.5 : f > 2 ? 2 : f;
        }
    }
}

/* one frame at the primary rate */
static void mix_one(float *lr)
{
    lr[0] = lr[1] = 0;
    for (int i = 0; i < MAX_BUFFERS; i++) {
        Dsb *b = &bufs[i];
        if (!b->used || !b->playing || !b->d->frames) continue;
        float g3, l3, r3;
        double pitch;
        place(b, &g3, &l3, &r3, &pitch);
        float g = db_gain(b->vol) * g3;
        float gl = g * l3 * (b->pan > 0 ? db_gain(-b->pan) : 1.0f), gr = g * r3 * (b->pan < 0 ? db_gain(b->pan) : 1.0f);
        const int16_t *s = b->d->pcm;
        uint32_t n = b->d->frames, k = (uint32_t)b->pos;
        if (k >= n) {
            if (!b->loop) { b->playing = false; b->pos = 0; continue; }
            b->pos = fmod(b->pos, n);
            k = (uint32_t)b->pos;
        }
        uint32_t k2 = k + 1 < n ? k + 1 : (b->loop ? 0 : k);
        float t = (float)(b->pos - k), l, r;
        if (b->channels == 2) {
            l = (s[2 * k] + (s[2 * k2] - s[2 * k]) * t) * (1.0f / 32768.0f);
            r = (s[2 * k + 1] + (s[2 * k2 + 1] - s[2 * k + 1]) * t) * (1.0f / 32768.0f);
        } else
            l = r = (s[k] + (s[k2] - s[k]) * t) * (1.0f / 32768.0f);
        lr[0] += l * gl;
        lr[1] += r * gr;
        b->pos += (double)b->freq * pitch / prim_rate;
    }
    for (int c = 0; c < 2; c++) {
        float v = lr[c] > 1.0f ? 1.0f : lr[c] < -1.0f ? -1.0f : lr[c];
        if (prim_bits == 8) v = floorf(v * 127.0f + 0.5f) / 127.0f;   /* the primary's 8 bits */
        lr[c] = v;
    }
}

void Dsb_Mix(float *out, unsigned frames)
{
    static float prev[2], cur[2];
    static double phase = 1;   /* where the output stands between prev and cur */
    for (unsigned f = 0; f < frames; f++) {
        while (phase >= 1) {
            prev[0] = cur[0], prev[1] = cur[1];
            mix_one(cur);
            phase -= 1;
        }
        float t = (float)phase;
        out[2 * f] = prev[0] + (cur[0] - prev[0]) * t;
        out[2 * f + 1] = prev[1] + (cur[1] - prev[1]) * t;
        phase += (double)prim_rate / OUT_RATE;
    }
}
