/* The car's files for the shell (shellcar.h, carfile.h): the readers' tables are the exe's, the training
   car reads with its parts and weight, and a .vsf written through the file layer reads back. Skips
   (exit 0) without data. */
#include "carfile.h"
#include "exe.h"
#include "res.h"
#include "shellcar.h"
#include "vfs.h"
#include "vfs_host.h"
#include "vfs_zfs.h"
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static int salvs;
static char salv_name[16];
static void on_salv(const char *name, int32_t count, int32_t flag)
{
    (void)flag;
    if (!salvs++) snprintf(salv_name, sizeof salv_name, "%s", name);
    CHECK(count == 77);
}

static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }

int main(void)
{
    char err[200];
    if (!vfs_mount_default() || !vfs_zfs_layer() || !exe_init(err, sizeof err)) {
        printf("shellcar_test: no game data, skipped\n");
        return 0;
    }
    Res_Init();
    CHECK(ShellCar_CheckTables());

    static uint8_t s[SHELL_VCF];
    CHECK(ShellCar_ReadVcf("vppa01.vcf", s));
    float w;
    memcpy(&w, s + 0xaa0, 4);
    printf("vppa01.vcf: \"%s\" body \"%s\" (%s, %s), engine \"%s\", brakes \"%s\", suspension \"%s\", "
           "%d hard points (%d read), weight %.1f\n", (char *)s + 0x50, (char *)s + 0xa0, (char *)s + 0xf0,
           (char *)s + 0xfd, (char *)s + 0x137, (char *)s + 0x147, (char *)s + 0x157, rd32(s + 0xa5c),
           rd32(s + 0xa60), w);
    for (int i = 0; i < rd32(s + 0xa60); i++)
        printf("  hard point %d \"%s\" id %d: \"%s\"\n", i, (char *)s + 0x378 + i * 0xfc,
               rd32(s + 0x428 + i * 0xfc), (char *)s + 0x3c8 + i * 0xfc);
    CHECK(s[0x50] && s[0x137] && s[0x147] && s[0x157] && rd32(s + 0xa60) > 0 && w > 0);

    Bwd2Out f;
    uint8_t vcst[0x4c] = {0}, wlst[0x18] = {0}, wp[0xc] = {0}, sv[0x18] = {0};
    vcst[0x3d] = 9;
    wlst[0x14] = 5;
    wp[0] = 3, wp[4] = 40;
    strcpy((char *)sv, "eng02");
    sv[0x10] = 77;
    CHECK(Bwd2_Create(&f, "addon\\shtest.vsf", 2));
    CHECK(Bwd2_Chunk(&f, 0x56435354, vcst, sizeof vcst) && Bwd2_Chunk(&f, 0x574c5354, wlst, sizeof wlst) &&
          Bwd2_Chunk(&f, 0x57505354, wp, sizeof wp) && Bwd2_Chunk(&f, 0x53414c56, sv, sizeof sv));
    CHECK(Bwd2_Close(&f, 1));
    CHECK(Res_AddPath("shtest.vsf", "addon"));
    int32_t st[0x4e] = {0};
    ShellCar_State(st, "shtest.vsf", on_salv);
    CHECK(st[0] == 9 && st[8] == 5 && st[9] == 3 && st[9 + 0x1e] == 40);
    CHECK(salvs == 1 && !strcmp(salv_name, "eng02"));
    printf("shellcar_test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
