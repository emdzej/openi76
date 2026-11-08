/* The exe's car functions the shell calls through its table (Shell_Load's +0x14, +0x18 and the rest;
   docs/re/carfile.md): a car's .vcf read for the garage into a scratch of the figures it shows (0x485210),
   a .vsf's state (0x485250, 0x48b420). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { SHELL_VCF = 0xaa8, SHELL_VSF = 0xc0 };

/* 0x485210: name's .vcf into the scratch (SHELL_VCF bytes, cleared by the caller): +0x50 the car's name,
   +0xa0 the body's, +0xf0 the .vdf, +0xfd the .vtf, +0x10a the shell's VSHL, +0x137/+0x147/+0x157 the
   engine's, brakes' and suspension's names, +0x168 the six WLOCs, +0x2b8/+0x2f8/+0x338 the wheel pairs,
   +0x378 the hard points (0xfc each, their guns' names at +0x50), +0xa5c the body's hard points, +0xa60
   the HLOCs read, +0xa64 SPCS, +0xa70 the specials, +0xa7c the armour, +0xaa0 the weight, +0xaa4 the
   body's */
bool ShellCar_ReadVcf(const char *name, uint8_t *scratch);

/* a .vsf's salvage item to its reader (the scratch's +0xb8) */
typedef void (*SalvFn)(const char *name, int32_t count, int32_t flag);

/* 0x485250: name's .vsf into the scratch (SHELL_VSF bytes): VCST at +0, WLST at +0x4c, the WPSTs at
   +0x64 (0xc each, their count at +0xbc), each SALV to the function at +0xb8 */
bool ShellCar_ReadVsf(const char *name, uint8_t *scratch, SalvFn salv);

/* 0x48b420 (the table's +0x18): name's .vsf as the shell's state of 0x4e dwords: [0] the engine's, [1]
   the brakes', [2] the suspension's condition, [3..8] the wheels', [9+i] gun i's hard point and [0x27+i]
   its count, the bytes at +0x114 the specials, [0x46..0x4d] the VCST's words from +0xd */
void ShellCar_State(int32_t out[0x4e], const char *name, SalvFn salv);

/* the port's tables are the exe's (tests) */
bool ShellCar_CheckTables(void);
