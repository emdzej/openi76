/* The resource layer: every data file by name (the memory/world module, 0x492470-0x4928c0, and the pak
   index 0x494ec0-0x4953b0).

   - Pak_Init 0x494ec0 reads every .pix of the file index, in the index's order, into one table of
     members (name, pak, offset, size), sorted by the first 16 characters ignoring case. A lookup is a
     binary search of that table: with the original's sort and search (crt.c), so that a name in two
     paks (11 do) resolves to the same pak.
   - Res_Get 0x492660 returns a file's bytes: "null..." is nothing, then a pak member (its .pak is read
     whole on first use and counted), then a file of its own, through a cache keyed by the exact name and
     counted. The original tries the cache first only when 0x5e2ff0 is set, and the command line
     (0x452a00) always clears it.
   - Res_Release 0x492570 drops a count. The original keeps a dropped file in the cache until memory
     runs short; the port keeps it until Res_Shutdown, which reads the same bytes either way. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool Res_Init(void);
void Res_Shutdown(void);
/* NULL if missing; *size (if given) gets the size */
const uint8_t *Res_Get(const char *name, size_t *size);
void Res_Release(const char *name);
/* Res_Exists 0x4928c0: in a pak, or a file of the index */
bool Res_Exists(const char *name);
/* Res_Size 0x492600: the size, 0 for none */
int32_t Res_Size(const char *name);
/* the pak holding a member, or NULL (for tools and tests) */
const char *Res_PakOf(const char *name);
/* 0x495b70 with 0x495bb0: name against a pattern (* ? [a-z] [!..] \x), case-sensitive; a leading
   wildcard never matches a name that begins with '.'. A '*' takes the name ending before the rest of the
   pattern as a match too (0x495d00 counts the -1 as found): "*.wdf" matches "wdf" */
bool Res_Match(const char *name, const char *pattern);
/* 0x48c220: the next name of the file index from *i that matches the pattern, *i past it; NULL after the
   last */
const char *Res_Next(const char *pattern, int *i);
/* 0x48c290: a written file into the file index, in the directory dir (a root added for it when new);
   the port also drops the name's cached bytes, so that the file is read again */
bool Res_AddPath(const char *name, const char *dir);
