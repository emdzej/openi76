/* File layer and ZFS layer against disc 1 (OPENI76_DATA: the disc folder or I76_CD1.ISO; default ./cd).
   Checks names in any case and with either separator, listing, the ZFS directory (3,847 members in v1.0)
   and members read through the layer. The archive's summary hash (FNV-1a over every name, size and the
   members' bytes) must equal tools/zfs.py --summary's when OPENI76_ZFS_SUMMARY is set. Skips (exit 0)
   without data. */
#include "vfs.h"
#include "vfs_host.h"
#include "vfs_zfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void count(const char *name, bool is_dir, void *user) { (void)name; (void)is_dir; ++*(int *)user; }

static uint32_t fnv(uint32_t h, const void *p, size_t n)
{
    for (const uint8_t *b = p; n--; b++) h = (h ^ *b) * 16777619u;
    return h;
}

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists("I76.EXE")) {
        printf("vfs_test: no game data, skipped\n");
        return 0;
    }
    printf("mounted: %s\n", vfs_describe());
    CHECK(vfs_exists("missions\\t01.MSN"));                    /* any case, either separator */
    CHECK(!vfs_exists("MISSIONS"));                            /* a directory is not a file */
    int k = 0;
    CHECK(vfs_list("Missions", count, &k) && k == 80);

    int n = vfs_zfs_count();
    CHECK(n == 3847);
    CHECK(vfs_exists("T05.ACT") && vfs_size("t05.act") == 768);   /* a member, any case */
    CHECK(!vfs_exists("missions/t05.act"));                     /* members have no directory */
    uint32_t h = 2166136261u;
    uint8_t *buf = NULL;
    size_t cap = 0;
    for (int i = 0; i < n; i++) {
        char name[32];
        uint32_t size;
        if (!vfs_zfs_entry(i, name, sizeof name, &size)) { CHECK(0); break; }
        h = fnv(h, name, strlen(name) + 1);
        h = fnv(h, &size, 4);   /* little-endian hosts only, as the tools */
        VfsFile *f = vfs_open(name);   /* v1.0: names are unique, none on the disc's root */
        if (!f) { CHECK(f); continue; }
        if (size > cap) buf = realloc(buf, cap = size);
        bool ok = vfs_file_size(f) == size && vfs_read_at(f, 0, buf, size) == (int64_t)size;
        CHECK(ok);
        if (ok) h = fnv(h, buf, size);
        vfs_close(f);
    }
    free(buf);
    printf("zfs: %d members, summary %08x\n", n, h);
    const char *want = getenv("OPENI76_ZFS_SUMMARY");
    if (want && *want) CHECK(strtoul(want, NULL, 16) == h);
    printf("vfs_test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
