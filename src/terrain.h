/* The terrain (0x48e060-0x491180, the zones 0x494bc0-0x494d50): a grid of 128 x 128 zones of 128 x 128 cells,
   5 m each. The mission's 80 x 80 zone map sits at zone (24, 24); the rest is the empty zone. A cell is a
   u16: the height in its low 12 bits (0.1 m), a flag in bit 12, a class in bits 13-15 (docs/re/terrain.md). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* 0x58a4c8: six names of 16 bytes, three per row of the terrain's texture levels */
extern char g_terrain_tex[6][16];

extern const uint16_t *g_zone_grid[128 * 128];   /* 0x5e3178 */
extern int8_t g_zone_map[80 * 80];               /* 0x58a8e8: a zone number per map square, -1 none */
extern int g_zone_count;                         /* 0x58a7a0 */
extern const uint16_t *g_zones[80];              /* 0x58a7a8 */
extern float g_cell, g_inv_cell;                 /* 0x5e30c0, 0x5e30c4: 5 m and 1 / 5 */
extern float g_terrain_lod;                      /* 0x4eb0d0 */
extern int g_terrain_on;                         /* 0x5e30cc */

/* Terrain_SetTextures 0x48e6a0: name's fifth character must be '1' (its six levels are that name with
   '1', '3', '5' in the first row and '2', '4', '6' in the second); an empty or invalid name gives the
   default six (the table at 0x4eb108, from the exe) with a message (a no-op). */
void Terrain_SetTextures(const char *name);

/* the TDEF chunk's handler 0x4845e0 / 0x494bc0: the zone map cleared to -1, the zones to none, then TREV,
   ZMAP, ZONE, EXIT (the table 0x4eb8e8) */
int Terrain_Tdef(const uint8_t *payload, void *name, const uint8_t *end);
/* Terrain_Setup 0x48e820: the level of detail (0x48e300), the cell size, and the zone grid (0x48e860) */
void Terrain_Setup(int on, int detail, float cell);
/* the .ter's mapping (0x494d20 frees the original's) */
void Terrain_Free(void);

/* Terrain_Height 0x491f00: the height at (x, z), bilinear between the four cells around it. It leaves the
   FPU rounding down (render/fpu.h). */
double Terrain_Height(double x, double z);
/* Terrain_Normal 0x491bf0: the ground's normal at (x, z), from one of the cell's two triangles (with the
   original's mistakes: the wrong fractions choose it, and the second's slopes have the wrong sign) */
void Terrain_Normal(float out[3], double x, double z);
/* Terrain_Class 0x48e380: the cell's top 3 bits */
int Terrain_Class(double x, double z);
/* Terrain_Flag 0x48e280: the cell's bit 12 */
int Terrain_Flag(double x, double z);
/* Terrain_Inside 0x48e210: (x, z) inside the grid's cells -3072..13311 */
bool Terrain_Inside(double x, double z);
/* Terrain_Line 0x45cfd0: 1 when the ground cuts the line from from (a point in doubles) to to, to then
   where (to the start when the line leaves the grid); 0 when clear, to unchanged */
int Terrain_Line(const double from[3], float to[3]);
