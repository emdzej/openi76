/* The terrain (0x48e060-0x491180): its texture names so far. */
#pragma once

/* 0x58a4c8: six names of 16 bytes, three per row of the terrain's texture levels */
extern char g_terrain_tex[6][16];

/* Terrain_SetTextures 0x48e6a0: name's fifth character must be '1' (its six levels are that name with
   '1', '3', '5' in the first row and '2', '4', '6' in the second); an empty or invalid name gives the
   default six (the table at 0x4eb108, from the exe) with a message (a no-op). */
void Terrain_SetTextures(const char *name);
