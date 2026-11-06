/* I76.ZFS layer (vfs_zfs.h). The archive's directory is read once at layering time into a table sorted
   by lower-case name; members are byte ranges of the archive, read through the medium below. */
#include "vfs_zfs.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { NAME_LEN = 16, ENTRY_SIZE = 36, HEADER_SIZE = 28 };

typedef struct {
    char name[NAME_LEN + 1];   /* as stored */
    char key[NAME_LEN + 1];    /* lower case, for the lookup */
    uint32_t off, size;
} Entry;

typedef struct {
    VfsBackend base;
    void *archive;             /* I76.ZFS, opened on the medium */
    Entry *ent;                /* archive order */
    Entry **sorted;            /* by key */
    int n;
} Zfs;

/* A layered file: a member (archive range) or a file of the medium. */
typedef struct {
    void *file;                /* the medium's handle; NULL for a member */
    uint64_t off, size;
} Handle;

static Zfs *layered;   /* the current layer, for vfs_zfs_count / _entry */

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static void lower(char *dst, const char *src, size_t cap)
{
    size_t i = 0;
    for (; src[i] && i + 1 < cap; i++) dst[i] = (char)(src[i] >= 'A' && src[i] <= 'Z' ? src[i] + 32 : src[i]);
    dst[i] = 0;
}

static int by_key(const void *a, const void *b) { return strcmp((*(Entry *const *)a)->key, (*(Entry *const *)b)->key); }

static bool read_exact(Zfs *z, uint64_t off, void *dst, size_t len)
{
    return z->base.read_at(z->base.ctx, z->archive, off, dst, len) == (int64_t)len;
}

static bool load_directory(Zfs *z, uint64_t archive_size)
{
    uint8_t h[HEADER_SIZE];
    if (!read_exact(z, 0, h, sizeof h) || memcmp(h, "ZFSF", 4)) return false;
    uint32_t version = rd32(h + 4), name_len = rd32(h + 8), per_block = rd32(h + 12), count = rd32(h + 16);
    uint32_t block = rd32(h + 24);
    if (version != 1 || name_len != NAME_LEN || !per_block || per_block > 1000 || count > 100000) return false;
    z->ent = calloc(count ? count : 1, sizeof *z->ent);
    z->sorted = calloc(count ? count : 1, sizeof *z->sorted);
    uint8_t *buf = malloc(4 + (size_t)per_block * ENTRY_SIZE);
    if (!z->ent || !z->sorted || !buf) { free(buf); return false; }
    int blocks = 0;
    while (block && (uint32_t)z->n < count) {
        size_t want = 4 + (size_t)per_block * ENTRY_SIZE;
        if (++blocks > 100000 || block + (uint64_t)want > archive_size || !read_exact(z, block, buf, want)) break;
        for (uint32_t i = 0; i < per_block && (uint32_t)z->n < count; i++) {
            const uint8_t *e = buf + 4 + (size_t)i * ENTRY_SIZE;
            Entry *d = &z->ent[z->n];
            memcpy(d->name, e, NAME_LEN);
            d->name[NAME_LEN] = 0;
            d->off = rd32(e + 16);
            d->size = rd32(e + 24);
            uint32_t flags = rd32(e + 32);
            if (flags || (uint64_t)d->off + d->size > archive_size) { free(buf); return false; }   /* compressed: a later version */
            lower(d->key, d->name, sizeof d->key);
            z->sorted[z->n] = d;
            z->n++;
        }
        block = rd32(buf);
    }
    free(buf);
    if ((uint32_t)z->n != count) return false;
    qsort(z->sorted, (size_t)z->n, sizeof *z->sorted, by_key);
    return true;
}

static const Entry *find(const Zfs *z, const char *rel)
{
    if (strchr(rel, '/') || strchr(rel, '\\')) return NULL;
    char key[64];
    lower(key, rel, sizeof key);
    int lo = 0, hi = z->n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2, c = strcmp(key, z->sorted[mid]->key);
        if (!c) return z->sorted[mid];
        if (c < 0) hi = mid - 1;
        else lo = mid + 1;
    }
    return NULL;
}

static void *zfs_open(void *ctx, const char *rel, uint64_t *size)
{
    Zfs *z = ctx;
    uint64_t s = 0;
    void *f = z->base.open(z->base.ctx, rel, &s);
    const Entry *e = f ? NULL : find(z, rel);
    if (!f && !e) return NULL;
    Handle *h = malloc(sizeof *h);
    if (!h) {
        if (f) z->base.close(z->base.ctx, f);
        return NULL;
    }
    *h = f ? (Handle){f, 0, s} : (Handle){NULL, e->off, e->size};
    *size = h->size;
    return h;
}

static int64_t zfs_read_at(void *ctx, void *file, uint64_t off, void *dst, size_t len)
{
    Zfs *z = ctx;
    Handle *h = file;
    if (h->file) return z->base.read_at(z->base.ctx, h->file, off, dst, len);
    if (off >= h->size) return 0;
    if (len > h->size - off) len = (size_t)(h->size - off);
    return z->base.read_at(z->base.ctx, z->archive, h->off + off, dst, len);
}

static void zfs_close(void *ctx, void *file)
{
    Zfs *z = ctx;
    Handle *h = file;
    if (h->file) z->base.close(z->base.ctx, h->file);
    free(h);
}

static bool zfs_list(void *ctx, const char *dir, VfsListFn fn, void *user)
{
    Zfs *z = ctx;
    return z->base.list(z->base.ctx, dir, fn, user);
}

static void zfs_free(Zfs *z)
{
    if (layered == z) layered = NULL;
    free(z->ent);
    free(z->sorted);
    free(z);
}

static void zfs_unmount(void *ctx)
{
    Zfs *z = ctx;
    z->base.close(z->base.ctx, z->archive);
    if (z->base.unmount) z->base.unmount(z->base.ctx);
    zfs_free(z);
}

bool vfs_zfs_layer(void)
{
    if (!vfs_exists("I76.ZFS")) return true;
    Zfs *z = calloc(1, sizeof *z);
    char desc[512];
    if (!z || !vfs_detach(&z->base, desc, sizeof desc)) { free(z); return false; }
    uint64_t size = 0;
    z->archive = z->base.open(z->base.ctx, "I76.ZFS", &size);
    if (!z->archive || !load_directory(z, size)) {
        if (z->archive) z->base.close(z->base.ctx, z->archive);
        vfs_mount(&z->base, desc);   /* hand the medium back */
        zfs_free(z);
        return false;
    }
    VfsBackend b = {z, zfs_open, zfs_read_at, zfs_close, zfs_list, zfs_unmount};
    char d[600];
    snprintf(d, sizeof d, "%s + I76.ZFS (%d files)", desc, z->n);
    vfs_mount(&b, d);
    layered = z;
    return true;
}

int vfs_zfs_count(void) { return layered ? layered->n : 0; }

bool vfs_zfs_entry(int i, char *name, size_t cap, uint32_t *size)
{
    if (!layered || i < 0 || i >= layered->n) return false;
    snprintf(name, cap, "%s", layered->ent[i].name);
    if (size) *size = layered->ent[i].size;
    return true;
}
