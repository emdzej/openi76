/* mw2.h: the shell's database */
#include "shell/mw2.h"
#include "vfs.h"
#include <stdlib.h>
#include <string.h>

struct Mw2 {
    VfsFile *f;
    int n;              /* +0x84 */
    uint32_t *at, *len; /* +0x88: each entry's offset and bytes */
};

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* i76shell 0x1001a510: the count, the offsets; an entry's bytes run to the next offset, the last's to the
   file's end */
Mw2 *Mw2_Open(const char *name)
{
    VfsFile *f = vfs_open(name);
    if (!f) return NULL;
    Mw2 *db = calloc(1, sizeof *db);
    uint8_t b[4];
    if (!db || vfs_read_at(f, 0, b, 4) != 4) goto bad;
    db->f = f;
    int32_t n = (int32_t)rd32(b);
    if (n < 0 || n > 100000) goto bad;
    db->n = n;
    db->at = calloc((size_t)n + 1, 4);
    db->len = calloc((size_t)n + 1, 4);
    uint8_t *t = malloc((size_t)n * 4 + 1);
    if (!db->at || !db->len || !t || vfs_read_at(f, 4, t, (size_t)n * 4) != (int64_t)n * 4) {
        free(t);
        goto bad;
    }
    for (int i = 0; i < n; i++) db->at[i] = rd32(t + 4 * i);
    free(t);
    uint64_t end = vfs_file_size(f);
    for (int i = 0; i < n; i++) db->len[i] = (i + 1 < n ? db->at[i + 1] : (uint32_t)end) - db->at[i];
    return db;
bad:
    if (db) {
        free(db->at);
        free(db->len);
        free(db);
    }
    vfs_close(f);
    return NULL;
}

void Mw2_Close(Mw2 *db)
{
    if (!db) return;
    vfs_close(db->f);
    free(db->at);
    free(db->len);
    free(db);
}

int Mw2_Count(const Mw2 *db) { return db ? db->n : 0; }

uint8_t *Mw2_Raw(Mw2 *db, int i, uint32_t *size)
{
    *size = 0;
    if (!db || i < 1 || i > db->n) return NULL;
    uint32_t n = db->len[i - 1];
    uint8_t *p = malloc(n ? n : 1);
    if (!p) return NULL;
    if (vfs_read_at(db->f, db->at[i - 1], p, n) != (int64_t)n) memset(p, 0, n);
    *size = n;
    return p;
}

/* The LZSS: a window of 4,096 bytes, zeros, written from its start; a flag byte's bits from the lowest,
   1 a literal, 0 a match of two bytes (the window's offset in the low 12 bits, the length less 3 in the
   high 4). Each match's length is taken from the count left, which can go below zero: the original
   writes the match whole past its block (here kept within it) */
uint8_t *Mw2_Unpack(Mw2 *db, int i, uint32_t *size)
{
    *size = 0;
    if (!db || i < 1 || i > db->n || db->len[i - 1] < 4) return NULL;
    uint32_t n = db->len[i - 1];
    uint8_t *src = malloc(n);
    if (!src || vfs_read_at(db->f, db->at[i - 1], src, n) != (int64_t)n) {
        free(src);
        return NULL;
    }
    int32_t left = (int32_t)rd32(src);
    if (left < 0 || left > 0x4000000) {
        free(src);
        return NULL;
    }
    uint8_t *out = malloc(left ? (size_t)left : 1);
    if (!out) {
        free(src);
        return NULL;
    }
    *size = (uint32_t)left;
    static uint8_t win[4096];   /* 0x1008a530 */
    memset(win, 0, sizeof win);
    uint32_t r = 0, at = 4, o = 0, flags = 2;
    while (left > 0) {
        flags >>= 1;
        if (flags == 1) flags = (at < n ? src[at++] : 0xff) | 0x100u;   /* fgetc's EOF reads as all ones */
        if (!(flags & 1)) {
            uint32_t b0 = at < n ? src[at++] : 0xff, b1 = at < n ? src[at++] : 0xff;
            uint32_t v = b0 + b1 * 0x100, p = v & 0xfff;
            int k = (int)(v >> 12) + 3;
            left -= k;
            while (k--) {
                uint8_t c = win[p];
                p = (p + 1) & 0xfff;
                win[r] = c;
                r = (r + 1) & 0xfff;
                if (o < *size) out[o] = c;
                o++;
            }
        } else {
            left--;
            uint8_t c = at < n ? src[at++] : 0xff;
            win[r] = c;
            r = (r + 1) & 0xfff;
            if (o < *size) out[o] = c;
            o++;
        }
    }
    free(src);
    return out;
}
