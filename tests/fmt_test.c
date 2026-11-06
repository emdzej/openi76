/* Format decoders (src/fmt/) against disc 1, each with a summary hash that must equal
   tools/i76fmt.py --summary's when its variable is set:
   - BWD2 (OPENI76_FMT_BWD2): every BWD2 file, I76.ZFS's in archive order, then the missions (MISSIONS, .msn) by name; FNV-1a
     over the name and each chunk's tag and payload size. Every file must tile exactly.
   - Images (OPENI76_FMT_IMAGES): every .map and .vqm of I76.ZFS in archive order, then every .vqm inside the
     .pak bundles (by .pix in archive order), decoded; FNV-1a over the name, u32 w, u32 h and the pixels.
     Every one must decode, every .act must be a palette, and every .pix must index its .pak.
   - Meshes (OPENI76_FMT_GEO): every .geo, loose in archive order then packed; FNV-1a over the name, the
     counts, the vertices and normals, and per face its rgb, flags, texture name and references.
   Skips (exit 0) without data. */
#include "fmt/bwd2.h"
#include "fmt/geo.h"
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
    /* the packed textures */
    char cb_name[13] = "";
    uint8_t *cb = NULL;
    size_t cb_n = 0;
    int paks = 0, members = 0;
    for (int i = 0; i < vfs_zfs_count(); i++) {
        char name[32], pak[32];
        uint32_t size;
        vfs_zfs_entry(i, name, sizeof name, &size);
        if (!has_ext(name, ".pix")) continue;
        snprintf(pak, sizeof pak, "%.*s.pak", (int)strlen(name) - 4, name);
        size_t tn, pn;
        uint8_t *t = vfs_read_all(name, &tn), *p = vfs_read_all(pak, &pn);
        PakEntry *e = NULL;
        int ne = 0;
        bool ok = t && p && pix_parse((const char *)t, tn, pn, &e, &ne);
        CHECK(ok);
        paks++;
        for (int k = 0; ok && k < ne; k++) {
            members++;
            if (!has_ext(e[k].name, ".vqm")) continue;
            const uint8_t *v = p + e[k].offset;
            char want_cb[13];
            if (!vqm_codebook_name(v, e[k].size, want_cb)) { CHECK(0); continue; }
            if (strcasecmp(want_cb, cb_name)) {
                free(cb);
                cb = vfs_read_all(want_cb, &cb_n);
                snprintf(cb_name, sizeof cb_name, "%s", want_cb);
            }
            Image8 im;
            bool dec = cb && vqm_decode(v, e[k].size, cb, cb_n, &im);
            CHECK(dec);
            if (!dec) { printf("  %s/%s\n", pak, e[k].name); continue; }
            uint32_t wh[2] = {(uint32_t)im.w, (uint32_t)im.h};
            h = fnv(h, e[k].name, strlen(e[k].name) + 1);
            h = fnv(h, wh, 8);
            h = fnv(h, im.pixels, (size_t)im.w * (size_t)im.h);
            image8_free(&im);
            count++;
        }
        free(e);
        free(t);
        free(p);
    }
    free(cb);
    printf("paks: %d, %d members\n", paks, members);
    CHECK(paks == 884 && members == 13585);
    CHECK(count == 5208 && acts == 23);
    printf("images: %d decoded, %d palettes\n", count, acts);
    want("OPENI76_FMT_IMAGES", h);
}

/* ---- meshes ---- */

static uint32_t geo_hash(uint32_t h, const char *name, const uint8_t *d, size_t n)
{
    Geo g;
    bool ok = geo_decode(d, n, &g);
    CHECK(ok);
    if (!ok) { printf("  %s\n", name); return h; }
    uint32_t c[2] = {(uint32_t)g.nverts, (uint32_t)g.nfaces};
    h = fnv(h, name, strlen(name) + 1);
    h = fnv(h, c, 8);
    h = fnv(h, g.verts, (size_t)g.nverts * sizeof *g.verts);
    h = fnv(h, g.normals, (size_t)g.nverts * sizeof *g.normals);
    for (int i = 0; i < g.nfaces; i++) {
        const GeoFace *f = &g.faces[i];
        h = fnv(h, f->rgb, 3);
        h = fnv(h, f->flags, 3);
        h = fnv(h, f->texture, strlen(f->texture) + 1);
        for (int k = 0; k < f->nrefs; k++) {
            uint32_t vn[2] = {f->refs[k].vertex, f->refs[k].normal};
            h = fnv(h, vn, 8);
            h = fnv(h, &f->refs[k].u, 4);
            h = fnv(h, &f->refs[k].v, 4);
        }
    }
    geo_free(&g);
    return h;
}

static void test_geo(void)
{
    uint32_t h = 2166136261u;
    int count = 0;
    for (int i = 0; i < vfs_zfs_count(); i++) {
        char name[32];
        uint32_t size;
        vfs_zfs_entry(i, name, sizeof name, &size);
        if (!has_ext(name, ".geo")) continue;
        size_t n;
        uint8_t *d = vfs_read_all(name, &n);
        if (d) h = geo_hash(h, name, d, n), count++;
        free(d);
    }
    for (int i = 0; i < vfs_zfs_count(); i++) {
        char name[32], pak[32];
        uint32_t size;
        vfs_zfs_entry(i, name, sizeof name, &size);
        if (!has_ext(name, ".pix")) continue;
        snprintf(pak, sizeof pak, "%.*s.pak", (int)strlen(name) - 4, name);
        size_t tn, pn;
        uint8_t *t = vfs_read_all(name, &tn), *p = vfs_read_all(pak, &pn);
        PakEntry *e = NULL;
        int ne = 0;
        if (t && p && pix_parse((const char *)t, tn, pn, &e, &ne))
            for (int k = 0; k < ne; k++)
                if (has_ext(e[k].name, ".geo")) h = geo_hash(h, e[k].name, p + e[k].offset, e[k].size), count++;
        free(e);
        free(t);
        free(p);
    }
    CHECK(count == 6888);
    printf("meshes: %d\n", count);
    want("OPENI76_FMT_GEO", h);
}

int main(void)
{
    if (!vfs_mount_default() || !vfs_zfs_count()) {
        printf("fmt_test: no game data, skipped\n");
        return 0;
    }
    test_bwd2();
    test_images();
    test_geo();
    printf("fmt_test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
