/* The mission file (.msn, BWD2): Mission_Load 0x484410 and its table 0x4e9770 (docs/re/world.md). */
#pragma once
#include <stdbool.h>
#include "chunks.h"

extern int g_mission_number;   /* 0x606e20: from the name (t01.msn: 1); 0x27 for a name not of the form */
extern int g_mission_kind;     /* 0x606e24: 1 m (melee), 2 t (training), 3 s (story); 0 other */

/* Mission_Load 0x484410: the kind and number from the name, then the file's chunks: WDEF (World_Wdef),
   TDEF (Terrain_Tdef), RDEF, ODEF, LDEF, ADEF (not ported yet: read past), EXIT. A missing file is the
   original's fatal message; here false. */
bool Mission_Load(const char *name);
/* for tests */
const ChunkDesc *Mission_Table(void);
