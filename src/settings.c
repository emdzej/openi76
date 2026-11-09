/* The player's settings (settings.h). */
#include "settings.h"
#include "exe.h"
#include "files.h"
#include "menu.h"
#include "physics.h"
#include "sound.h"
#include "vfs.h"
#include <stdlib.h>
#include <string.h>

Detail g_detail;

void Settings_Defaults(int cls)
{
    if (cls < 0 || cls > 3) cls = 3;
    memcpy(g_detail.b, exe_data(0x4daf38u + 14u * (uint32_t)cls, 14), 14);
}

static uint8_t head[0x40];       /* 0x5e1490: 0x58d820's copy (not held by the port: zeros, or the file's) */
static uint8_t more[2] = {1, 0}; /* 0x5e14e2, 0x5e14e3 */

static void pack(uint8_t *b)
{
    memcpy(b, head, 0x40);
    memcpy(b + 0x40, g_detail.b, 14);
    memcpy(b + 0x4e, g_sound_set, 4);
    memcpy(b + 0x52, more, 2);
    memcpy(b + 0x54, &g_game_options, 4);
}

void Settings_Init(void)
{
    memset(head, 0, sizeof head);   /* the block cleared */
    g_game_options = 0;
    Settings_Defaults(3);   /* 0x44e260 */
    Sound_Defaults();       /* 3, 5, 10, 8 */
    more[0] = 1, more[1] = 0;
    size_t n = 0;
    uint8_t *f = vfs_read_all(exe_str(0x4dad80), &n);
    if (f) {   /* fread of 0x58 bytes: a shorter file fills what it has */
        uint8_t b[0x58];
        pack(b);
        memcpy(b, f, n < sizeof b ? n : sizeof b);
        free(f);
        memcpy(head, b, 0x40);
        memcpy(g_detail.b, b + 0x40, 14);
        memcpy(g_sound_set, b + 0x4e, 4);
        memcpy(more, b + 0x52, 2);
        memcpy(&g_game_options, b + 0x54, 4);
    }
    /* a screen mode below 5 when not full screen goes to 5 and is written (the port is full screen: the
       flag 0x4d66e0 set, so never) */
    if (g_game_options & 0x1c) g_cheater = 1;
    /* 0x467100 and 0x4671d0 (the voices and the volumes) run in the port's Sound_Init */
}

/* Settings_Save 0x44d7b0 (the shell's i76shell 0x10039f30 writes the same file from its copy) */
void Settings_Save(void)
{
    uint8_t b[0x58];
    pack(b);
    Files_Write(exe_str(0x4dad80), b, sizeof b);   /* I76PLYR.DEF */
}

void Settings_GetMore(uint8_t out[2]) { memcpy(out, more, 2); }
void Settings_SetMore(const uint8_t in[2]) { memcpy(more, in, 2); }
