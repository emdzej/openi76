/* The world and terrain definitions (world.h, terrain.h) on disc 1's missions: the WDEF and TDEF tables
   are the exe's, every mission's terrain maps its zones, and every mission's WDEF reads, with a palette, a light environment, the six terrain texture names and the names it gives
   all present. Skips (exit 0) without data. */
#include "chunks.h"
#include "exe.h"
#include "res.h"
#include "terrain.h"
#include "vfs.h"
#include "vfs_host.h"
#include "vfs_zfs.h"
#include "world.h"
#include "render/light.h"
#include "render/palette.h"
#include "settings.h"
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

const ChunkDesc *World_Table(void);
const ChunkDesc *Terrain_Table(void);
static int wdef_seen;
static int h_wdef(const uint8_t *p, void *a, const uint8_t *e) { wdef_seen++; return World_Wdef(p, a, e); }
static int h_any(const uint8_t *c, void *a, const uint8_t *e) { (void)c, (void)a, (void)e; return 1; }

int main(void)
{
    char err[200];
    if (!vfs_mount_default() || !vfs_zfs_layer() || !exe_init(err, sizeof err)) {
        printf("world_test: no game data, skipped\n");
        return 0;
    }
    Res_Init();
    Gamma_Build();
    Settings_Defaults(3);
    CHECK(Chunks_Check(World_Table(), 3, 0x4eb5e8));
    CHECK(Chunks_Check(Terrain_Table(), 4, 0x4eb8e8));
    ChunkDesc top[] = {
        {"WDEF", h_wdef, 0x2006}, {"TDEF", Terrain_Tdef, 0x2006}, {"RDEF", h_any, 0x2006}, {"ODEF", h_any, 0x2006},
        {"LDEF", h_any, 0x2006}, {"ADEF", h_any, 0x2006}, {"EXIT", h_any, 0x2000},
    };
    static const char *const kinds[] = {"t%02d.msn", "m%02d.msn", "s%02d.msn"};
    int read = 0, good = 0, envs[8] = {0};
    for (int k = 0; k < 3; k++)
        for (int i = 1; i <= 17; i++) {
            char name[16];
            snprintf(name, sizeof name, kinds[k], i);
            if (!Res_Exists(name)) continue;
            read++;
            memset(&g_world, 0, sizeof g_world);
            memset(g_world_pal, 0, sizeof g_world_pal);
            wdef_seen = 0;
            bool ok = Bwd2_Read(name, top, 7, NULL) && wdef_seen == 1;
            ok &= g_light_env >= 0 && g_light_env < 8 && g_sun_map != NULL;
            int nonzero = 0;
            for (int j = 0; j < 768; j++) nonzero |= g_world_pal[j];
            ok &= nonzero != 0 && g_shown_pal[0] == 0 && g_shown_pal[1] == 0 && g_shown_pal[2] == 0;
            for (int t = 0; t < 6; t++) ok &= Res_Exists(g_terrain_tex[t]);
            ok &= !g_world.sky[0] || Res_Exists(g_world.sky);
            char zm[16];   /* its fifth character is the screen's (0x4929c0): '3' at 320 x 240 */
            memcpy(zm, g_world.zmap, sizeof zm);
            zm[4] = '3';
            ok &= !zm[0] || Res_Exists(zm);
            /* the terrain: zones mapped, the map's squares within the count, the grid holding them */
            int squares = 0;
            for (int q = 0; q < 80 * 80; q++) squares += g_zone_map[q] >= 0;
            ok &= g_zone_count > 0 && g_zones[g_zone_count - 1] != NULL && squares > 0;
            ok &= g_zone_grid[24 * 128 + 24] != NULL && g_inv_cell > 0.19f && g_inv_cell < 0.21f;
            if (ok) {
                good++;
                envs[g_light_env & 7]++;
            } else
                printf("%s: env %d, sky %s, zmap %s, tex %s %s, zones %d\n", name, g_light_env, g_world.sky,
                       g_world.zmap, g_terrain_tex[0], g_terrain_tex[5], g_zone_count);
            Light_Shutdown();
        }
    printf("%d of %d worlds read; environments", good, read);
    for (int e = 0; e < 8; e++) printf(" %d", envs[e]);
    printf("\n");
    CHECK(read >= 39 && good == read);
    printf("world_test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
