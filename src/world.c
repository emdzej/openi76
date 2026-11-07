/* The world definition (world.h). */
#include "world.h"
#include "chunks.h"
#include "res.h"
#include "terrain.h"
#include "render/light.h"
#include "render/palette.h"
#include "render/surface.h"
#include <stdint.h>
#include <string.h>

World g_world;
uint8_t g_world_pal[768];
uint8_t g_shown_pal[768];

static int32_t rd32(const uint8_t *p) { return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24); }

/* the original's strcpy of a name field; the port stops at its buffer */
static void name(char *dst, size_t cap, const uint8_t *src)
{
    strncpy(dst, (const char *)src, cap - 1);
    dst[cap - 1] = 0;
}

int World_Wrld(const uint8_t *c, void *arg, const uint8_t *end)
{
    (void)arg, (void)end;
    const uint8_t *p = c + 8;
    char pal_name[14], lum_name[14], tbl_name[14];
    name(pal_name, sizeof pal_name, p + 0x1e);
    name(lum_name, sizeof lum_name, p + 0x2b);
    name(tbl_name, sizeof tbl_name, p + 0x38);
    g_world.u0 = (uint32_t)rd32(p);
    Light_SetHour(rd32(p + 0x86));
    const uint8_t *pal = Res_Get(pal_name, NULL), *lum = Res_Get(lum_name, NULL);
    if (pal) {
        memcpy(g_world_pal, pal, 768);
        Display_SetPalette(g_shown_pal, 0, 256, pal, lum);   /* through the display's function 0x58dc5c */
    }
    if (lum) Res_Release(lum_name);
    if (pal) Res_Release(pal_name);
    const uint8_t *tbl = Res_Get(tbl_name, NULL);
    if (tbl) {   /* missing: a message (a no-op) */
        Remap_Reset();
        memcpy(g_rtab + RT_TBL, tbl, 0x10000);
        Res_Release(tbl_name);
    }
    Remap_Key(0xff);
    memcpy(g_world.lights, p + 0x8a, sizeof g_world.lights);
    name(g_world.sky, sizeof g_world.sky, p + 0x52);
    name(g_world.npt, sizeof g_world.npt, p + 0x45);
    name(g_world.zmap, sizeof g_world.zmap, p + 0x79);
    name(g_world.scrounge, sizeof g_world.scrounge, p + 0x5f);   /* 0x494a30, not ported yet */
    Terrain_SetTextures((const char *)p + 0x6c);
    memcpy(g_world.intro, p + 4, 13);
    memcpy(g_world.outro, p + 0x11, 13);
    g_world.range = (float)rd32(p + 0x12a);
    return 1;
}

/* EXIT 0x4843d0: compares the chunk's tag with the entry's address, which never match: it fails, and the
   walk, at an EXIT, ends anyway */
static int exit_check(const uint8_t *c, void *arg, const uint8_t *end)
{
    (void)c, (void)arg, (void)end;
    return 0;
}

static ChunkDesc wdef[] = {   /* 0x4eb5e8 */
    {"WREV", Rev_Check, 0x2004}, {"WRLD", World_Wrld, 0x1}, {"EXIT", exit_check, 0x1000},
};

int World_Wdef(const uint8_t *payload, void *name_arg, const uint8_t *end)
{
    return Bwd2_Walk(payload, wdef, 3, NULL, name_arg, true, end) != NULL;
}

/* for tests */
const ChunkDesc *World_Table(void) { return wdef; }
