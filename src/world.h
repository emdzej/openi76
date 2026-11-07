/* The world definition (WDEF, 0x492fa0-0x4931e0): a mission's first chunk, naming its palette, tables,
   sky, terrain textures, time of day and lighting (docs/re/world.md). */
#pragma once
#include <stdint.h>

typedef struct {
    uint32_t u0;           /* 0x4dc7a0 (0x4686b0): payload +0 */
    char intro[13];        /* 0x58dc90: payload +4, a movie */
    char outro[13];        /* 0x58dca0: payload +0x11 */
    char npt[16];          /* 0x5ad300: payload +0x45 */
    char sky[16];          /* 0x58c490: payload +0x52, the sky's .map */
    char scrounge[16];     /* payload +0x5f, a .sdf (0x494a30, not ported yet) */
    char zmap[16];         /* 0x4db010: payload +0x79 */
    uint8_t lights[0xa0];  /* 0x5e3020 (0x4931e0): payload +0x8a, 8 records of 5 dwords */
    float range;           /* 0x4a8470: payload +0x12a, an int; the view's range when 0x5e14da is set */
} World;

extern World g_world;
extern uint8_t g_world_pal[768];   /* 0x4d6900: the palette as read (before gamma) */
extern uint8_t g_shown_pal[768];   /* the display record's +0xe0: the gamma-corrected palette shown */

/* the WDEF chunk's handler 0x4845c0 / 0x492fa0: walks WREV, WRLD, EXIT (the table 0x4eb5e8) */
int World_Wdef(const uint8_t *payload, void *name, const uint8_t *end);
/* WRLD 0x492fd0 (the chunk, header included) */
int World_Wrld(const uint8_t *c, void *arg, const uint8_t *end);
