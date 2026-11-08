/* smacker.h: the port's smackw32.dll, from the format (docs/formats/smk.md) */
#include "smacker.h"
#include "exe.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DLL_SIZE 71680u
#define DLL_CRC32 0xf1e9c672u
#define DLL_RAMP 0xe87d   /* the 64 bytes of the palette's ramp, in the file (0x40fa7d) */

static uint8_t ramp[64];
static bool tables;

bool Smk_LoadTables(char *err, size_t cap)
{
    if (tables) return true;
    size_t n = 0;
    uint8_t *d = vfs_read_all(SMK_DLL, &n);
    if (!d || n != DLL_SIZE || crc32(d, n) != DLL_CRC32) {
        if (err) snprintf(err, cap, "%s: %s", SMK_DLL, d ? "unsupported version" : "not found");
        free(d);
        return false;
    }
    memcpy(ramp, d + DLL_RAMP, sizeof ramp);
    free(d);
    tables = true;
    return true;
}

/* ---- the bits: least significant first ---- */

typedef struct {
    const uint8_t *p;
    uint32_t n, at;   /* bits */
} Bits;

static int bit(Bits *b)
{
    if (b->at >= b->n) return 0;
    int v = b->p[b->at >> 3] >> (b->at & 7) & 1;
    b->at++;
    return v;
}

static uint32_t bits(Bits *b, int n)
{
    uint32_t v = 0;
    for (int i = 0; i < n; i++) v |= (uint32_t)bit(b) << i;
    return v;
}

/* ---- the trees ---- */

/* a byte tree: node i's two branches in kid[i][0..1], a leaf as 0x8000 | its byte; empty, every code is 0
   and reads nothing */
typedef struct {
    uint16_t kid[512][2];
    int n;
    bool on;
} Tree8;

static int tree8_node(Tree8 *t, Bits *b, int depth)
{
    if (!bit(b)) return 0x8000 | (int)bits(b, 8);
    if (depth > 32 || t->n >= 512) return 0x8000;
    int i = t->n++;
    int k0 = tree8_node(t, b, depth + 1);
    t->kid[i][0] = (uint16_t)k0;
    int k1 = tree8_node(t, b, depth + 1);
    t->kid[i][1] = (uint16_t)k1;
    return i;
}

/* a tree's present bit, its nodes, and the 0 bit after them */
static void tree8_read(Tree8 *t, Bits *b)
{
    t->n = 0;
    t->on = bit(b);
    if (!t->on) return;
    int root = tree8_node(t, b, 0);
    if (root & 0x8000) {   /* a single leaf: its code is empty */
        t->kid[0][0] = t->kid[0][1] = (uint16_t)root;
        t->n = 1;
    }
    bit(b);
}

static int tree8_get(const Tree8 *t, Bits *b)
{
    if (!t->on) return 0;
    int i = 0;
    if (t->n == 1 && t->kid[0][0] & 0x8000) return t->kid[0][0] & 0xff;
    for (int k = 0; k < 32; k++) {
        int v = t->kid[i][bit(b)];
        if (v & 0x8000) return v & 0xff;
        i = v;
    }
    return 0;
}

/* a 16-bit tree: its leaves' values made of two byte trees' codes; three escape values mark the leaves
   that stand for the last three values decoded (each frame starts them at 0) */
typedef struct {
    int32_t (*kid)[2];   /* a leaf as -1 - its index */
    uint16_t *val;
    int nodes, leaves, cap;
    int esc[3];
    bool on;
} Tree16;

static int tree16_node(Tree16 *t, Bits *b, const Tree8 *lo, const Tree8 *hi, const uint16_t e[3], int depth)
{
    if (!bit(b)) {
        if (t->leaves >= t->cap) return -1;
        int v = tree8_get(lo, b);
        v |= tree8_get(hi, b) << 8;
        int l = t->leaves++;
        for (int k = 0; k < 3; k++)
            if (v == e[k]) {
                t->esc[k] = l;
                v = 0;
                break;
            }
        t->val[l] = (uint16_t)v;
        return -1 - l;
    }
    if (depth > 64 || t->nodes >= t->cap) return -1;
    int i = t->nodes++;
    int k0 = tree16_node(t, b, lo, hi, e, depth + 1);
    t->kid[i][0] = k0;
    int k1 = tree16_node(t, b, lo, hi, e, depth + 1);
    t->kid[i][1] = k1;
    return i;
}

static bool tree16_read(Tree16 *t, Bits *b, uint32_t size)
{
    memset(t, 0, sizeof *t);
    t->on = bit(b);
    if (!t->on) return true;
    static Tree8 lo, hi;
    tree8_read(&lo, b);
    tree8_read(&hi, b);
    uint16_t e[3];
    for (int k = 0; k < 3; k++) e[k] = (uint16_t)bits(b, 16), t->esc[k] = -1;
    t->cap = (int)(size / 4) + 4;
    t->kid = calloc((size_t)t->cap, sizeof *t->kid);
    t->val = calloc((size_t)t->cap + 3, sizeof *t->val);
    if (!t->kid || !t->val) return false;
    int root = tree16_node(t, b, &lo, &hi, e, 0);
    bit(b);
    if (root < 0) {   /* a single leaf */
        t->kid[0][0] = t->kid[0][1] = root;
        t->nodes = 1;
    }
    for (int k = 0; k < 3; k++)
        if (t->esc[k] < 0) t->esc[k] = t->leaves++;
    return true;
}

static void tree16_free(Tree16 *t)
{
    free(t->kid);
    free(t->val);
}

static void tree16_reset(Tree16 *t)
{
    if (t->on)
        for (int k = 0; k < 3; k++) t->val[t->esc[k]] = 0;
}

static int tree16_get(Tree16 *t, Bits *b)
{
    if (!t->on) return 0;
    int i = 0, l = -1;
    if (t->nodes == 1 && t->kid[0][0] < 0) l = -1 - t->kid[0][0];
    for (int k = 0; l < 0 && k < 64; k++) {
        int v = t->kid[i][bit(b)];
        if (v < 0) l = -1 - v;
        else i = v;
    }
    if (l < 0) return 0;
    int v = t->val[l];
    if (v != t->val[t->esc[0]]) {
        t->val[t->esc[2]] = t->val[t->esc[1]];
        t->val[t->esc[1]] = t->val[t->esc[0]];
        t->val[t->esc[0]] = (uint16_t)v;
    }
    return v;
}

/* ---- the movie ---- */

enum { T_MMAP, T_MCLR, T_FULL, T_TYPE };

struct Smk {
    VfsFile *f;
    int w, h;
    uint32_t frames, num;
    int32_t rate;
    uint32_t flags;
    uint32_t audio_max[7], audio_rate[7];
    uint32_t *size;      /* each frame's bytes (the low two bits are flags) */
    uint8_t *type;       /* each frame's chunks: bit 0 a palette, bits 1-7 the audio tracks */
    uint64_t *at;        /* each frame's offset */
    Tree16 tree[4];
    uint8_t *pixels;
    uint8_t pal[768];
    bool new_pal;
    uint8_t *audio;
    uint32_t audio_bytes;
    uint8_t *buf;
    uint32_t buf_cap;
    double t0;
    bool started;
};

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

void Smk_Close(Smk *s)
{
    if (!s) return;
    for (int i = 0; i < 4; i++) tree16_free(&s->tree[i]);
    if (s->f) vfs_close(s->f);
    free(s->size);
    free(s->type);
    free(s->at);
    free(s->pixels);
    free(s->audio);
    free(s->buf);
    free(s);
}

Smk *Smk_Open(const char *path)
{
    if (!tables && !Smk_LoadTables(NULL, 0)) return NULL;
    VfsFile *f = vfs_open(path);
    if (!f) return NULL;
    Smk *s = calloc(1, sizeof *s);
    if (!s) {
        vfs_close(f);
        return NULL;
    }
    s->f = f;
    uint8_t h[104];
    if (vfs_read_at(f, 0, h, sizeof h) != (int64_t)sizeof h || memcmp(h, "SMK2", 4)) goto bad;
    s->w = (int)rd32(h + 4), s->h = (int)rd32(h + 8), s->frames = rd32(h + 12);
    s->rate = (int32_t)rd32(h + 16), s->flags = rd32(h + 20);
    if (s->w <= 0 || s->h <= 0 || s->w > 2048 || s->h > 2048 || (s->w | s->h) & 3 || !s->frames || s->frames > 100000)
        goto bad;
    uint32_t n = s->frames + (s->flags & 1);   /* the ring frame, after the last */
    for (int i = 0; i < 7; i++) s->audio_max[i] = rd32(h + 24 + 4 * i), s->audio_rate[i] = rd32(h + 72 + 4 * i);
    uint32_t trees = rd32(h + 52), tsize[4];
    for (int i = 0; i < 4; i++) tsize[i] = rd32(h + 56 + 4 * i);
    s->size = calloc(n, 4);
    s->type = calloc(n, 1);
    s->at = calloc(n, 8);
    s->pixels = calloc((size_t)s->w * s->h, 1);
    if (!s->size || !s->type || !s->at || !s->pixels) goto bad;
    uint8_t *t = malloc((size_t)n * 5);
    if (!t || vfs_read_at(f, 104, t, (size_t)n * 5) != (int64_t)n * 5) {
        free(t);
        goto bad;
    }
    for (uint32_t i = 0; i < n; i++) s->size[i] = rd32(t + 4 * i), s->type[i] = t[4 * n + i];
    free(t);
    uint8_t *td = malloc(trees + 4);
    if (!td || vfs_read_at(f, 104 + (uint64_t)n * 5, td, trees) != (int64_t)trees) {
        free(td);
        goto bad;
    }
    memset(td + trees, 0, 4);
    Bits b = {td, trees * 8, 0};
    bool ok = true;
    for (int i = 0; i < 4; i++) ok &= tree16_read(&s->tree[i], &b, tsize[i]);
    free(td);
    if (!ok) goto bad;
    uint64_t at = 104 + (uint64_t)n * 5 + trees;
    for (uint32_t i = 0; i < n; i++) s->at[i] = at, at += s->size[i] & ~3u;
    return s;
bad:
    Smk_Close(s);
    return NULL;
}

int Smk_Width(const Smk *s) { return s->w; }
int Smk_Height(const Smk *s) { return s->h; }
uint32_t Smk_Frames(const Smk *s) { return s->frames; }
uint32_t Smk_FrameNum(const Smk *s) { return s->num; }
const uint8_t *Smk_Pixels(const Smk *s) { return s->pixels; }
bool Smk_NewPalette(const Smk *s) { return s->new_pal; }
const uint8_t *Smk_Palette(const Smk *s) { return s->pal; }
int Smk_AudioRate(const Smk *s) { return s->audio_rate[0] & 0x40000000 ? (int)(s->audio_rate[0] & 0xffffff) : 0; }
int Smk_AudioBits(const Smk *s) { return s->audio_rate[0] & 0x20000000 ? 16 : 8; }
int Smk_AudioChannels(const Smk *s) { return s->audio_rate[0] & 0x10000000 ? 2 : 1; }

const uint8_t *Smk_Audio(const Smk *s, uint32_t *bytes)
{
    *bytes = s->audio_bytes;
    return s->audio;
}

/* the rate: above 0 a frame's milliseconds, below 0 in hundredths of them, 0 ten frames a second */
double Smk_FrameMs(const Smk *s)
{
    if (s->rate > 0) return s->rate;
    if (s->rate < 0) return -s->rate / 100.0;
    return 100.0;
}

/* the palette's chunk: a byte with 0x80 skips (its low seven bits + 1) entries, with 0x40 copies (its low
   six + 1) from the previous palette at the next byte's index, else it and the next two bytes are a new
   entry's red, green and blue through the ramp */
static void palette(Smk *s, const uint8_t *p, uint32_t n)
{
    uint8_t old[768];
    memcpy(old, s->pal, sizeof old);
    uint32_t i = 0;
    for (int e = 0; e < 256 && i < n;) {
        uint8_t c = p[i++];
        if (c & 0x80) e += (c & 0x7f) + 1;
        else if (c & 0x40) {
            if (i >= n) break;
            int from = p[i++], k = (c & 0x3f) + 1;
            if (from + k > 256) break;
            while (k-- && e < 256) memcpy(s->pal + 3 * e++, old + 3 * from++, 3);
        } else {
            if (i + 2 > n) break;
            s->pal[3 * e] = ramp[c & 0x3f];
            s->pal[3 * e + 1] = ramp[p[i++] & 0x3f];
            s->pal[3 * e + 2] = ramp[p[i++] & 0x3f];
            e++;
        }
    }
}

/* a compressed track's chunk: its unpacked size, then bits: data present, stereo, 16-bit, a byte tree for
   each channel's (and each byte's) deltas, the channels' first samples (the right's first, a 16-bit one's
   high byte first), then each sample as the last plus its delta */
static void audio(Smk *s, const uint8_t *p, uint32_t n)
{
    s->audio_bytes = 0;
    if (!(s->audio_rate[0] & 0x80000000)) {   /* raw PCM */
        if (!(s->audio = realloc(s->audio, n ? n : 1))) return;
        memcpy(s->audio, p, n);
        s->audio_bytes = n;
        return;
    }
    if (n < 4) return;
    uint32_t out = rd32(p);
    Bits b = {p + 4, (n - 4) * 8, 0};
    if (!bit(&b)) return;
    int stereo = bit(&b), wide = bit(&b);
    if (out > 0x1000000 || !(s->audio = realloc(s->audio, out ? out : 1))) return;
    static Tree8 tr[4];
    for (int i = 0; i < 1 << (stereo + wide); i++) tr[i].on = false;
    for (int i = 0; i < 1 << (stereo + wide); i++) tree8_read(&tr[i], &b);
    int pred[2] = {0, 0};
    uint8_t *d = s->audio;
    if (wide) {
        for (int c = stereo; c >= 0; c--) {
            int v = (int)bits(&b, 8) << 8;
            v |= (int)bits(&b, 8);
            pred[c] = (int16_t)v;
        }
        uint32_t i = 0, ns = out / 2;
        for (int c = 0; c <= stereo && i < ns; c++, i++) d[2 * i] = (uint8_t)pred[c], d[2 * i + 1] = (uint8_t)(pred[c] >> 8);
        for (; i < ns; i++) {
            int c = (int)(i & (uint32_t)stereo);
            int v = tree8_get(&tr[2 * c], &b);
            v |= tree8_get(&tr[2 * c + 1], &b) << 8;
            pred[c] = (int16_t)(pred[c] + (int16_t)v);
            d[2 * i] = (uint8_t)pred[c], d[2 * i + 1] = (uint8_t)(pred[c] >> 8);
        }
    } else {
        for (int c = stereo; c >= 0; c--) pred[c] = (int)bits(&b, 8);
        uint32_t i = 0;
        for (int c = 0; c <= stereo && i < out; c++, i++) d[i] = (uint8_t)pred[c];
        for (; i < out; i++) {
            int c = (int)(i & (uint32_t)stereo);
            pred[c] = (uint8_t)(pred[c] + (int8_t)tree8_get(&tr[c], &b));
            d[i] = (uint8_t)pred[c];
        }
    }
    s->audio_bytes = out;
}

/* the video: the 4 x 4 blocks in rows, in runs. A type code's low two bits are the kind, its next six the
   run's length (1 to 59, then 128 to 2048 by doubling), its high byte a fill's colour */
static void video(Smk *s, const uint8_t *p, uint32_t n)
{
    for (int i = 0; i < 4; i++) tree16_reset(&s->tree[i]);
    Bits b = {p, n * 8, 0};
    int bw = s->w / 4, blocks = bw * (s->h / 4), blk = 0, stride = s->w;
    while (blk < blocks && b.at < b.n) {
        int type = tree16_get(&s->tree[T_TYPE], &b);
        int r = type >> 2 & 0x3f, run = r < 59 ? r + 1 : 128 << (r - 59);
        switch (type & 3) {
        case 0:   /* two colours by a bit map */
            for (; run-- && blk < blocks; blk++) {
                int clr = tree16_get(&s->tree[T_MCLR], &b), map = tree16_get(&s->tree[T_MMAP], &b);
                uint8_t hi = (uint8_t)(clr >> 8), lo = (uint8_t)clr;
                uint8_t *o = s->pixels + (blk / bw) * 4 * stride + (blk % bw) * 4;
                for (int y = 0; y < 4; y++, o += stride, map >>= 4)
                    for (int x = 0; x < 4; x++) o[x] = map >> x & 1 ? hi : lo;
            }
            break;
        case 1:   /* every pixel: each row's right pair, then its left */
            for (; run-- && blk < blocks; blk++) {
                uint8_t *o = s->pixels + (blk / bw) * 4 * stride + (blk % bw) * 4;
                for (int y = 0; y < 4; y++, o += stride) {
                    int v = tree16_get(&s->tree[T_FULL], &b);
                    o[2] = (uint8_t)v, o[3] = (uint8_t)(v >> 8);
                    v = tree16_get(&s->tree[T_FULL], &b);
                    o[0] = (uint8_t)v, o[1] = (uint8_t)(v >> 8);
                }
            }
            break;
        case 2:   /* the previous frame's */
            blk += run;
            break;
        case 3:   /* one colour */
            for (; run-- && blk < blocks; blk++) {
                uint8_t *o = s->pixels + (blk / bw) * 4 * stride + (blk % bw) * 4;
                for (int y = 0; y < 4; y++, o += stride) memset(o, type >> 8, 4);
            }
            break;
        }
    }
}

/* a frame's chunks: the palette (its first byte x 4 bytes, that byte with them), each flagged track's (a
   dword of its bytes with it, then the data), the video (the rest) */
bool Smk_DoFrame(Smk *s)
{
    s->new_pal = false;
    s->audio_bytes = 0;
    uint32_t n = s->size[s->num] & ~3u;
    if (n + 4 > s->buf_cap) {
        uint8_t *nb = realloc(s->buf, n + 4);
        if (!nb) return false;
        s->buf = nb, s->buf_cap = n + 4;
    }
    if (vfs_read_at(s->f, s->at[s->num], s->buf, n) != (int64_t)n) return false;
    memset(s->buf + n, 0, 4);
    const uint8_t *p = s->buf, *end = s->buf + n;
    uint8_t type = s->type[s->num];
    if (type & 1) {
        uint32_t k = (uint32_t)p[0] * 4;
        if (!k || k > (uint32_t)(end - p)) return false;
        palette(s, p + 1, k - 1);
        s->new_pal = true;
        p += k;
    }
    for (int t = 0; t < 7; t++)
        if (type & 2 << t) {
            if (end - p < 4) return false;
            uint32_t k = rd32(p);
            if (k < 4 || k > (uint32_t)(end - p)) return false;
            if (t == 0) audio(s, p + 4, k - 4);
            p += k;
        }
    video(s, p, (uint32_t)(end - p));
    return true;
}

void Smk_NextFrame(Smk *s)
{
    if (++s->num >= s->frames + (s->flags & 1)) s->num = 0;
}

/* the clock starts at the first call: the current frame is due then */
bool Smk_Wait(Smk *s, double now_ms)
{
    if (!s->started) s->started = true, s->t0 = now_ms - (double)s->num * Smk_FrameMs(s);
    return now_ms < s->t0 + (double)s->num * Smk_FrameMs(s);
}
