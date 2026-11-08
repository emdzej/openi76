/* The car's files for the shell (shellcar.h, carfile.h): the readers' tables are the exe's, the training
   car reads with its parts and weight, and a .vsf written through the file layer reads back. Skips
   (exit 0) without data. */
#include "carfile.h"
#include "chunks.h"
#include "vehicle.h"
#include "exe.h"
#include "res.h"
#include "shellcar.h"
#include "shell/trip.h"
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
    CHECK(Chunks_Check(Vehicle_VsfTable(), 5, 0x4e9ff8));

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

    static uint8_t recs[2 * SHELL_REC];
    CHECK(ShellCar_Record(recs, "vppa01.vcf", 0) == 4);
    int slots = rd32(recs + 0x3c);
    printf("record: \"%s\" / \"%s\", %d slots, .vdf %s, wheels %s %s %s\n", (char *)recs, (char *)recs + 0x14,
           slots, (char *)recs + 0x820, (char *)recs + 0x83a, (char *)recs + 0x847, (char *)recs + 0x854);
    for (int i = 0; i < slots && i < 24; i++) {
        const uint8_t *sl = recs + 0x40 + 0x20 * i;
        printf("  slot %2d kind %2d %-10s at %3d,%3d: %s\n", i, rd32(sl), (char *)sl + 4, rd32(sl + 0x14),
               rd32(sl + 0x18), (char *)recs + 0x400 + 0x1e * i);
    }
    CHECK(slots >= 3 + 4 && rd32(recs + 0x40) == 2 && rd32(recs + 0x864) == 4);

    static uint8_t cat[SHELL_PARTS * SHELL_PART];
    int parts = ShellCar_Catalogue(cat), by[16] = {0};
    for (int i = 0; i < parts; i++) {
        int k = rd32(cat + i * SHELL_PART + 0x1e);
        if (k >= 0 && k < 16) by[k]++;
    }
    printf("catalogue: %d parts; engines %d, suspensions %d, brakes %d, wheels %d, guns %d+%d+%d, specials %d, "
           "paints %d\n", parts, by[2], by[3], by[4], by[5], by[7], by[8], by[9], by[0xd], by[0xe]);
    for (int i = 10; i < parts; i += parts / 8)
        printf("  %3d %-20s kind %2d %s %s\n", i, (char *)cat + i * SHELL_PART, rd32(cat + i * SHELL_PART + 0x1e),
               (char *)cat + i * SHELL_PART + 0x2e, (char *)cat + i * SHELL_PART + 0x3b);
    CHECK(by[2] == 5 && by[3] == 5 && by[4] == 5 && by[0xd] == 10 && by[5] > 1 && by[7] > 1 && by[0xe] > 1);

    /* the record back as a .vcf, read again */
    ShellCar_WriteVcf(recs, cat, parts, "addon\\shtest.vcf", 0);
    CHECK(Res_AddPath("shtest.vcf", "addon"));
    static uint8_t s2[SHELL_VCF];
    CHECK(ShellCar_ReadVcf("shtest.vcf", s2));
    printf("written back: \"%s\" %s %s, engine \"%s\", guns %s %s %s %s\n", (char *)s2 + 0x50, (char *)s2 + 0xf0,
           (char *)s2 + 0xfd, (char *)s2 + 0x137, (char *)s2 + 0x3c8, (char *)s2 + 0x3c8 + 0xfc,
           (char *)s2 + 0x3c8 + 2 * 0xfc, (char *)s2 + 0x3c8 + 3 * 0xfc);
    CHECK(!strcmp((char *)s2 + 0x50, (char *)s + 0x50) && !strcmp((char *)s2 + 0xf0, (char *)s + 0xf0) &&
          !strcmp((char *)s2 + 0x137, (char *)s + 0x137) && !strcmp((char *)s2 + 0x3c8, (char *)s + 0x3c8) &&
          !strcmp((char *)s2 + 0x3c8 + 3 * 0xfc, (char *)s + 0x3c8 + 3 * 0xfc));

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
    /* the shell's NEW TRIP: vppt01.vcf's record written by the shell (0x1001d6a0, 0x1001de30), read back
       by the exe's reader as the original car */
    CHECK(dll_init(err, sizeof err));
    if (dll_loaded()) {
        static uint8_t t1[SHELL_VCF], t2[SHELL_VCF];
        CHECK(ShellCar_ReadVcf("vppt01.vcf", t1));
        g_sh_car = 0;
        CHECK(ShellCar_Record(g_sh_recs, "vppt01.vcf", 0) > 0);
        g_sh_ncat = ShellCar_Catalogue(g_sh_cat);
        Parts_Lists();
        Car_WriteVcf("addon\\shtest2.vcf", NULL);
        Car_WriteVsf("addon\\shtest2.vsf", NULL, NULL, NULL, NULL);
        Parts_ListsFree();
        CHECK(ShellCar_ReadVcf("shtest2.vcf", t2));
        printf("the shell's vppt01: \"%s\" %s, engine \"%s\" brakes \"%s\" suspension \"%s\", wheels %s %s, "
               "%d hard points: %s %s\n", (char *)t2 + 0x50, (char *)t2 + 0xf0, (char *)t2 + 0x137,
               (char *)t2 + 0x147, (char *)t2 + 0x157, (char *)t2 + 0x2b8, (char *)t2 + 0x338, rd32(t2 + 0xa60),
               (char *)t2 + 0x3c8, (char *)t2 + 0x3c8 + 0xfc);
        CHECK(!strcmp((char *)t2 + 0x50, (char *)t1 + 0x50) && !strcmp((char *)t2 + 0xf0, (char *)t1 + 0xf0));
        CHECK(!strcmp((char *)t2 + 0x137, (char *)t1 + 0x137) && !strcmp((char *)t2 + 0x147, (char *)t1 + 0x147) &&
              !strcmp((char *)t2 + 0x157, (char *)t1 + 0x157));
        CHECK(!memcmp(t2 + 0x2b8, t1 + 0x2b8, 0x40) && !memcmp(t2 + 0x338, t1 + 0x338, 0x40));
        CHECK(rd32(t2 + 0xa60) == rd32(t1 + 0xa60));
        for (int i = 0; i < rd32(t1 + 0xa60); i++) {
            printf("  hard point %d: \"%s\" (the file's \"%s\")\n", i, (char *)t2 + 0x3c8 + i * 0xfc, (char *)t1 + 0x3c8 + i * 0xfc);
            CHECK(!strcmp((char *)t2 + 0x3c8 + i * 0xfc, (char *)t1 + 0x3c8 + i * 0xfc));
        }
        int32_t ts[0x4e] = {0};
        ShellCar_State(ts, "shtest2.vsf", NULL);
        printf("  its state: engine %d brakes %d suspension %d, wheels %d %d %d %d, gun 0 on %d (%d)\n", ts[0], ts[1],
               ts[2], ts[3], ts[4], ts[7], ts[8], ts[9], ts[0x27]);
        CHECK(ts[0] > 0 && ts[1] > 0 && ts[2] > 0 && ts[3] > 0 && ts[0x27] > 0);
    }
    printf("shellcar_test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
