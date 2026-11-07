/* The mission file (mission.h). */
#include "mission.h"
#include "crt.h"
#include "entity.h"
#include "objects.h"
#include "exe.h"
#include "platform.h"
#include "roads.h"
#include "terrain.h"
#include "world.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int g_mission_number, g_mission_kind;

/* the handlers of the steps not ported yet (ADEF 0x484660 0x492f70, the mission's script): the chunk is
   read past */
static int later(const uint8_t *c, void *arg, const uint8_t *end)
{
    (void)c, (void)arg, (void)end;
    return 1;
}

/* EXIT 0x484820: outside a network game (0x407380: 0x4a8760 or the shell's 0x461900; neither is ported,
   so never), a mission without the user's vehicle is the original's fatal */
static int mission_exit(const uint8_t *c, void *arg, const uint8_t *end)
{
    (void)c, (void)end;
    if (!g_user_entity) {
        char msg[160];
        snprintf(msg, sizeof msg, exe_str(0x4e9ad8), arg ? (const char *)arg : "");
        plat_log(msg);
    }
    return 1;
}

static ChunkDesc table[] = {   /* 0x4e9770 */
    {"WDEF", World_Wdef, 0x2006}, {"TDEF", Terrain_Tdef, 0x2006}, {"RDEF", Roads_Rdef, 0x2006},
    {"ODEF", Objects_Odef, 0x2006},      {"LDEF", Objects_Ldef, 0x2006},        {"ADEF", later, 0x2006},
    {"EXIT", mission_exit, 0x2000},
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
