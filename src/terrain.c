/* The terrain (terrain.h). */
#include "terrain.h"
#include "exe.h"
#include <string.h>

char g_terrain_tex[6][16];

static void set(int i, const char *s)
{
    /* the original's strcpy: a name longer than 15 runs into the next slot; the port cuts it */
    strncpy(g_terrain_tex[i], s, 15);
    g_terrain_tex[i][15] = 0;
}

void Terrain_SetTextures(const char *name)
{
    if (!*name || strlen(name) < 5 || name[4] != '1') {
        for (int i = 0; i < 6; i++) set(i, exe_str(exe_u32(0x4eb108u + 4u * (uint32_t)i)));
        return;
    }
    for (int i = 0; i < 6; i++) set(i, name);
    for (int i = 0; i < 3; i++) {
        g_terrain_tex[i][4] = (char)('1' + 2 * i);
        g_terrain_tex[3 + i][4] = (char)('2' + 2 * i);
    }
}
