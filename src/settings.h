/* The player's settings (0x5e1490, 0x58 bytes, saved as I76PLYR.DEF; 0x44d660): so far the detail block at
   +0x40 (0x5e14d0, 14 bytes). Its defaults are one of four presets (0x4daf38, from the exe) by the machine:
   below 133 MHz class 0, below 166 MHz class 1 or 2 (by memory), else class 3 (0x44e260). The port is a fast
   machine: class 3. */
#pragma once
#include <stdint.h>

typedef struct {
    uint8_t b[14];   /* 0x5e14d0 */
} Detail;
extern Detail g_detail;

enum {
    DET_SCREEN = 0,    /* 0x5e14d0: the screen mode (the zmap's resolution digit, 0x4eb557) */
    DET_SKY = 1,       /* 0x5e14d1: the textured sky and the sun (Sky_Init 0x407a10, Sky_Draw's mode 2) */
    DET_SHADOWS = 6,   /* 0x5e14d6: objects' shadows (0x424b90: 1 the simplest level, 2 by distance); the
                          shadow mask's surface 0x58d890 is cleared before every frame (0x407550) */
    DET_TERRAIN = 7,   /* 0x5e14d7: terrain detail 0-2 (0x48e300) */
    DET_RANGE = 0xa,   /* 0x5e14da: the world's view range (else 150) */
    DET_TERRTEX = 0xb, /* 0x5e14db: the second terrain renderer (0x48ffa0) and its textures */
    DET_GAMMA = 0xc,   /* 0x5e14dc: the gamma row, 1-10 */
};

/* Settings_Defaults 0x44e260 for a machine class 0-3 (the exe's presets) */
void Settings_Defaults(int cls);
