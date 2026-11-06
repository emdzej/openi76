/* PCX decoding (pcx.h) of disc 1's loading screens: 640 x 480, every byte of the RLE stream consumed.
   Prints an FNV-1a hash of the pixels and the palette, which ffmpeg's decode reproduces (tools/pcx-ref.py).
   Skips (exit 0) without data. */
#include "pcx.h"
#include "vfs.h"
#include "vfs_host.h"
#include <stdio.h>
#include <stdlib.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static uint32_t fnv(uint32_t h, const uint8_t *b, size_t n)
{
    while (n--) h = (h ^ *b++) * 16777619u;
    return h;
}

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists("LOADSCR.PCX")) {
        printf("pcx_test: no game data, skipped\n");
        return 0;
    }
    static const char *const names[] = {"LOADSCR.PCX", "LOADGAME.PCX"};
    for (int i = 0; i < 2; i++) {
        size_t n;
        uint8_t *d = vfs_read_all(names[i], &n);
        Pcx p;
        bool ok = d && pcx_decode(d, n, &p);
        CHECK(ok);
        if (ok) {
            CHECK(p.w == 640 && p.h == 480);
            printf("%s: %dx%d pixels %08x palette %08x\n", names[i], p.w, p.h,
                   fnv(2166136261u, p.pixels, (size_t)p.w * (size_t)p.h), fnv(2166136261u, p.pal, 768));
            pcx_free(&p);
        }
        free(d);
    }
    printf("pcx_test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
