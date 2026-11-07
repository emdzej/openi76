/* The resource layer (res.h) on disc 1: every pak member reads back as its .pak's bytes at the .pix's
   offset, "null" names are nothing, a file of its own comes through the cache, and the names in two paks
   resolve as the original's index does (tools/emu/diff_crt.py checks the sort itself). Skips (exit 0)
   without data. */
#include "res.h"
#include "vfs.h"
#include "vfs_host.h"
#include "vfs_zfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void)
{
    if (!vfs_mount_default() || !vfs_zfs_layer() || vfs_zfs_count() == 0) {
        printf("res_test: no game data, skipped\n");
        return 0;
    }
    CHECK(Res_Init());
    size_t n = 0;
    const uint8_t *act = Res_Get("t05.act", &n);   /* a file of its own */
    CHECK(act && n == 768);
    const uint8_t *again = Res_Get("t05.act", NULL);
    CHECK(again == act);
    Res_Release("t05.act");
    Res_Release("t05.act");
    CHECK(Res_Get("NULL", NULL) == NULL && Res_Get("nullgeo", NULL) == NULL);
    CHECK(!Res_Exists("no-such-file.xyz"));
    /* a mission: through the index's "missions" root */
    const uint8_t *msn = Res_Get("t01.msn", &n);
    CHECK(msn && n > 8 && memcmp(msn, "BWD2", 4) == 0 && Res_Exists("T01.TER"));
    Res_Release("t01.msn");

    /* every member of one bundle against its pak, read directly */
    const char *pak = Res_PakOf("burnwood.vqm");   /* also in buildcmn.pak; the original's search finds this */
    CHECK(pak && strcmp(pak, "aobilb0m.pak") == 0);
    if (pak) {
        char pix[20];
        snprintf(pix, sizeof pix, "%s", pak);
        memcpy(strchr(pix, '.'), ".pix", 5);
        size_t tn, pn;
        char *text = (char *)vfs_read_all(pix, &tn);
        uint8_t *bytes = vfs_read_all(pak, &pn);
        CHECK(text && bytes);
        if (text && bytes) {
            int count = 0, ok = 0, used = 0;
            char *p = text;
            sscanf(p, "%d%n", &count, &used);
            p += used;
            for (int i = 0; i < count; i++) {
                char name[64];
                int off, size;
                if (sscanf(p, "%63s %d %d%n", name, &off, &size, &used) != 3) break;
                p += used;
                size_t got;
                const uint8_t *d = Res_Get(name, &got);
                if (strcmp(Res_PakOf(name), pak) != 0) ok++;   /* the other copy of a doubled name */
                else if (d && got == (size_t)size && memcmp(d, bytes + off, (size_t)size) == 0) ok++;
                Res_Release(name);
            }
            CHECK(ok == count);
            printf("%s: %d members\n", pak, count);
        }
        free(text);
        free(bytes);
    }
    printf("burnwood.vqm: %s (two paks hold it)\n", pak ? pak : "?");
    Res_Shutdown();
    printf("res_test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
