/* The mission file (mission.h). */
#include "mission.h"
#include "crt.h"
#include "terrain.h"
#include "world.h"
#include <stdlib.h>
#include <string.h>

int g_mission_number, g_mission_kind;

/* the handlers of the steps not ported yet (0x484600 roads 0x492cf0, 0x484620 objects 0x494140, 0x484640
   0x493530, 0x484660 0x492f70): the chunk is read past */
static int later(const uint8_t *c, void *arg, const uint8_t *end)
{
    (void)c, (void)arg, (void)end;
    return 1;
}

static ChunkDesc table[] = {   /* 0x4e9770 */
    {"WDEF", World_Wdef, 0x2006}, {"TDEF", Terrain_Tdef, 0x2006}, {"RDEF", later, 0x2006},
    {"ODEF", later, 0x2006},      {"LDEF", later, 0x2006},        {"ADEF", later, 0x2006},
    {"EXIT", later, 0x2000},
};

bool Mission_Load(const char *name)
{
    /* 0x494a10 (the scrounge objects' reset) is not ported yet */
    char base[16], *dot;
    strncpy(base, name, 15);
    base[15] = 0;
    const char *file = strrchr(base, '\\') ? strrchr(base, '\\') + 1 : base;
    char stem[16];
    strncpy(stem, file, 15);
    stem[15] = 0;
    g_mission_kind = 0;
    g_mission_number = 0x27;
    if ((dot = strrchr(stem, '.')) && dot - stem == 3 && crt_stricmp(dot, ".msn") == 0) {
        *dot = 0;
        g_mission_number = atoi(stem + 1);
        if (g_mission_number > 0)
            g_mission_kind = stem[0] == 'm' ? 1 : stem[0] == 't' ? 2 : stem[0] == 's' ? 3 : 0;
    }
    /* a name without an extension gets ".msn"; 0x48c480 and 0x484bc0 / 0x468800 (the CD) are not ported */
    char path[24];
    strncpy(path, base, sizeof path - 5);
    path[sizeof path - 5] = 0;
    if (!strrchr(file, '.')) strcat(path, ".msn");
    return Bwd2_Read(path, table, 7, NULL);
}

const ChunkDesc *Mission_Table(void) { return table; }
