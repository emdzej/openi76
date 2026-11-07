/* The texture manager (tex.h) on disc 1: every .tmt of the paks makes a material whose frames all have
   pixels, each decoded .vqm matches fmt/image.c's decoder, frames switch with Tex_SetFrame, and the
   pixel cache is shared between owners. Skips (exit 0) without data. */
#include "fmt/image.h"
#include "res.h"
#include "tex.h"
#include "vfs.h"
#include "vfs_host.h"
#include "vfs_zfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* every member name of every .pix with the extension */
static int members(const char *ext, char (*out)[16], int cap)
{
    int k = 0;
    for (int i = 0; i < vfs_zfs_count() && k < cap; i++) {
        char nm[64];
        uint32_t sz;
        if (!vfs_zfs_entry(i, nm, sizeof nm, &sz) || !strstr(nm, ".pix")) continue;
        size_t n;
        char *t = (char *)vfs_read_all(nm, &n);
        char *save = NULL, *line = t ? strtok_r(t, "\r\n", &save) : NULL;
        while (line && (line = strtok_r(NULL, "\r\n", &save)) && k < cap) {
            char name[64];
            if (sscanf(line, "%63s", name) == 1 && strlen(name) < 16 && strstr(name, ext)) snprintf(out[k++], 16, "%s", name);
        }
        free(t);
    }
    return k;
}

int main(void)
{
    if (!vfs_mount_default() || !vfs_zfs_layer() || vfs_zfs_count() == 0) {
        printf("tex_test: no game data, skipped\n");
        return 0;
    }
    Res_Init();
    static char names[8000][16];
    int n = members(".TMT", names, 8000), ok = 0;
    for (int i = 0; i < n; i++)
        if (Tex_Get(1, names[i])) ok++;
    printf("%d of %d .tmt with pixels\n", ok, n);
    CHECK(n > 1000 && ok == n);

    /* the decoder against fmt/image.c's, on 400 .vqm */
    int nv = members(".vqm", names, 8000), same = 0, tried = 0;
    for (int i = 0; i < nv; i += nv / 400 + 1) {
        char map[20];
        snprintf(map, sizeof map, "%.*s.map", (int)(strchr(names[i], '.') - names[i]), names[i]);
        const Texture *t = Tex_Pixels(map);
        size_t vn, cn;
        const uint8_t *v = Res_Get(names[i], &vn);
        char cbk[13] = {0};
        Image8 im = {0};
        bool dec = v && vqm_codebook_name(v, vn, cbk);
        const uint8_t *c = dec ? Res_Get(cbk, &cn) : NULL;
        dec = c && vqm_decode(v, vn, c, cn, &im);
        tried++;
        if (t && dec && t->w == im.w && t->h == im.h && memcmp(t->pixels, im.pixels, (size_t)im.w * im.h) == 0) same++;
        image8_free(&im);
        Tex_PixelsRelease(map);
    }
    printf("%d of %d .vqm decode as fmt/image.c does\n", same, tried);
    CHECK(same == tried);

    /* frames, and the cache shared by two owners */
    const char *anim = "xbd1_101.tmt";
    const Texture *a = Tex_Get(7, anim), *b = Tex_Get(8, anim);
    CHECK(a && a == b);
    CHECK(Tex_SetFrame(7, anim, 0, 5, true));
    const Texture *c = Tex_Get(7, anim);
    CHECK(c && c != a && Tex_Get(8, anim) == a);
    Tex_Shutdown();
    Res_Shutdown();
    printf("tex_test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
