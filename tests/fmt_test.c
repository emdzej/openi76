/* Format decoders (src/fmt/) against disc 1, each with a summary hash that must equal
   tools/i76fmt.py --summary's when its variable is set:
   - BWD2 (OPENI76_FMT_BWD2): every BWD2 file, I76.ZFS's in archive order, then the missions (MISSIONS, .msn) by name; FNV-1a
     over the name and each chunk's tag and payload size. Every file must tile exactly.
   - Images (OPENI76_FMT_IMAGES): every .map and .vqm of I76.ZFS in archive order, decoded; FNV-1a over the
     name, u32 w, u32 h and the pixels. Every one must decode, and every .act must be a palette.
   Skips (exit 0) without data. */
#include "fmt/bwd2.h"
#include "fmt/image.h"
#include "vfs.h"
#include "vfs_host.h"
#include "vfs_zfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static uint32_t fnv(uint32_t h, const void *p, size_t n)
{
    for (const uint8_t *b = p; n--; b++) h = (h ^ *b) * 16777619u;
    return h;
}

static void want(const char *var, uint32_t h)
{
    const char *w = getenv(var);
    printf("%s=%08x%s\n", var, h, w && *w ? (strtoul(w, NULL, 16) == h ? " (matches)" : " (MISMATCH)") : "");
    if (w && *w) CHECK(strtoul(w, NULL, 16) == h);
}

/* ---- BWD2 ---- */

static uint32_t bwd2_hash(uint32_t h, const char *name, const uint8_t *d, size_t n, int *count)
{
    if (!bwd2_is(d, n)) return h;
    CHECK(bwd2_valid(d, n));
    h = fnv(h, name, strlen(name) + 1);
    size_t pos = 0;
    Bwd2Chunk c;
    while (bwd2_next(d, n, &pos, &c)) {
        uint32_t s = c.size;
        h = fnv(h, c.tag, 4);   /* "REV\0" keeps its NUL */
        h = fnv(h, &s, 4);
    }
    ++*count;
    return h;
}

typedef struct { char (*names)[32]; int n, cap; } Names;
static void add_msn(const char *name, bool is_dir, void *user)
{
    Names *l = user;
    size_t k = strlen(name);
    if (is_dir || k < 4 || strcasecmp(name + k - 4, ".msn")) return;
    if (l->n == l->cap) l->names = realloc(l->names, (size_t)(l->cap = l->cap ? 2 * l->cap : 64) * sizeof *l->names);
    snprintf(l->names[l->n++], 32, "%s", name);
}
static int by_name(const void *a, const void *b) { return strcmp(a, b); }

static void test_bwd2(void)
{
    uint32_t h = 2166136261u;
    int count = 0;
    for (int i = 0; i < vfs_zfs_count(); i++) {
        char name[32];
        uint32_t size;
        vfs_zfs_entry(i, name, sizeof name, &size);
        size_t n;
        uint8_t *d = vfs_read_all(name, &n);
        if (d) h = bwd2_hash(h, name, d, n, &count);
        free(d);
    }
    Names l = {0};
    CHECK(vfs_list("MISSIONS", add_msn, &l));
    qsort(l.names, (size_t)l.n, sizeof *l.names, by_name);
    for (int i = 0; i < l.n; i++) {
        char path[48];
        snprintf(path, sizeof path, "MISSIONS/%s", l.names[i]);
        size_t n;
        uint8_t *d = vfs_read_all(path, &n);
        CHECK(d != NULL);
        if (d) h = bwd2_hash(h, l.names[i], d, n, &count);
        free(d);
    }
    free(l.names);
    CHECK(count == 854);
    printf("bwd2: %d files\n", count);
    want("OPENI76_FMT_BWD2", h);
}

/* ---- images ---- */

static bool has_ext(const char *name, const char *ext)
{
    size_t n = strlen(name), k = strlen(ext);
    return n > k && !strcasecmp(name + n - k, ext);
}

static void test_images(void)
{
    uint32_t h = 2166136261u;
    int count = 0, acts = 0;
    for (int i = 0; i < vfs_zfs_count(); i++) {
        char name[32];
        uint32_t size;
        vfs_zfs_entry(i, name, sizeof name, &size);
        bool map = has_ext(name, ".map"), vqm = has_ext(name, ".vqm"), act = has_ext(name, ".act");
        if (!map && !vqm && !act) continue;
        size_t n;
        uint8_t *d = vfs_read_all(name, &n);
        if (!d) { CHECK(d); continue; }
        if (act) {
            uint8_t pal[768];
            CHECK(act_decode(d, n, pal));
            acts++;
            free(d);
            continue;
        }
        Image8 im;
        bool ok;
        if (vqm) {
            char cb[13];
            size_t cn = 0;
            uint8_t *c = vqm_codebook_name(d, n, cb) ? vfs_read_all(cb, &cn) : NULL;
            ok = c && vqm_decode(d, n, c, cn, &im);
            free(c);
        } else
            ok = map_decode(d, n, &im);
        free(d);
        CHECK(ok);
        if (!ok) { printf("  %s\n", name); continue; }
        uint32_t wh[2] = {(uint32_t)im.w, (uint32_t)im.h};
        h = fnv(h, name, strlen(name) + 1);
        h = fnv(h, wh, 8);
        h = fnv(h, im.pixels, (size_t)im.w * (size_t)im.h);
        image8_free(&im);
        count++;
    }
    CHECK(count == 391 && acts == 23);
    printf("images: %d decoded, %d palettes\n", count, acts);
    want("OPENI76_FMT_IMAGES", h);
}

int main(void)
{
    if (!vfs_mount_default() || !vfs_zfs_count()) {
        printf("fmt_test: no game data, skipped\n");
        return 0;
    }
    test_bwd2();
    test_images();
    printf("fmt_test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
