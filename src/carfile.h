/* The car's files between a trip's missions (docs/re/carfile.md): the BWD2 writer (0x4848d0-0x484a30),
   the .vsf a won mission leaves (0x4878b0, with the salvage's SALV chunks 0x488db0) and the files the
   shell's garage reads and writes through the exe (0x487c90-0x48b7f0). */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* a BWD2 file being written (the original's FILE *): kept in memory, written out at the close */
typedef struct {
    char path[96];
    uint8_t *b;
    size_t n, cap;
    bool bad;
} Bwd2Out;

/* 0x4848d0: "BWD2" and the revision's chunk ("REV", rev); false if it cannot be made */
bool Bwd2_Create(Bwd2Out *f, const char *path, int32_t rev);
/* 0x484980: a chunk, its tag the four letters of tag from the high byte, its size the data's + 8 (data
   NULL: the header alone, as EXIT's) */
bool Bwd2_Chunk(Bwd2Out *f, uint32_t tag, const void *data, size_t size);
/* 0x484a30: EXIT when exit is set, then the file written (Files_Write) */
bool Bwd2_Close(Bwd2Out *f, int exit);

extern int g_salvage_none;   /* 0x58dc84: the mission is t12, whose salvage is not kept */

/* 0x4878b0 (WinMain's, a won mission of a trip): the user's car as dir\name (addon\vehscn.vsf) after the
   salvage's pick (0x489000): VCST, WLST, WPST, the salvage's SALV, EXIT. 1 written or not a trip */
int Vsf_WriteMission(const char *dir, const char *name);
